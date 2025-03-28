// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

namespace System.Net.Sockets
{
    public partial class SafeSocketHandle
    {
        private int _receiveTimeout = -1;
        private int _sendTimeout = -1;
        private SocketAsyncContext? _asyncContext;

        [Flags]
        private enum Flags : byte
        {
            NonBlocking = 1,
            LastConnectFailed = 2,
            DualMode = 4,
            ExposedHandleOrUntrackedConfiguration = 8,
            PreferInlineCompletions = 16,
            IsSocket = 32,
            IsDisconnected = 64,
#if SYSTEM_NET_SOCKETS_APPLE_PLATFROM
            TfoEnabled = 128
#endif
        }

        private Flags _flags = Flags.IsSocket | (SocketAsyncEngine.InlineSocketCompletionsEnabled ? Flags.PreferInlineCompletions : 0);

        private void SetFlag(Flags flag, bool value)
        {
            if (value) _flags |= flag;
            else _flags &= ~flag;
        }

        internal bool LastConnectFailed
        {
            get => (_flags & Flags.LastConnectFailed) != 0;
            set => SetFlag(Flags.LastConnectFailed, value);
        }

        internal bool DualMode
        {
            get => (_flags & Flags.DualMode) != 0;
            set => SetFlag(Flags.DualMode, value);
        }

        internal bool ExposedHandleOrUntrackedConfiguration
        {
            get => (_flags & Flags.ExposedHandleOrUntrackedConfiguration) != 0;
            private set => SetFlag(Flags.ExposedHandleOrUntrackedConfiguration, value);
        }

        internal bool PreferInlineCompletions
        {
            get => (_flags & Flags.PreferInlineCompletions) != 0;
            set => SetFlag(Flags.PreferInlineCompletions, value);
        }

        // (ab)use Socket class for performing async I/O on non-socket fds.
        internal bool IsSocket
        {
            get => (_flags & Flags.IsSocket) != 0;
            set => SetFlag(Flags.IsSocket, value);
        }

#if SYSTEM_NET_SOCKETS_APPLE_PLATFROM
        internal bool TfoEnabled
        {
            get => (_flags & Flags.TfoEnabled) != 0;
            set => SetFlag(Flags.TfoEnabled, value);
        }
#endif

        internal void RegisterConnectResult(SocketError error)
        {
            switch (error)
            {
                case SocketError.Success:
                case SocketError.WouldBlock:
                    break;
                default:
                    LastConnectFailed = true;
                    break;
            }
        }

        internal void TransferTrackedState(SafeSocketHandle target)
        {
            target._trackedOptions = _trackedOptions;
            target.LastConnectFailed = LastConnectFailed;
            target.DualMode = DualMode;
            target.ExposedHandleOrUntrackedConfiguration = ExposedHandleOrUntrackedConfiguration;
            target.IsSocket = IsSocket;
#if SYSTEM_NET_SOCKETS_APPLE_PLATFROM
            target.TfoEnabled = TfoEnabled;
#endif
        }

        internal void SetExposed() => ExposedHandleOrUntrackedConfiguration = true;

        internal SocketAsyncContext AsyncContext =>
            _asyncContext ??
            Interlocked.CompareExchange(ref _asyncContext, new SocketAsyncContext(this), null) ??
            _asyncContext!;

        /// <summary>
        /// This represents whether the Socket instance is blocking or non-blocking *from the user's point of view*,
        /// i.e. it corresponds to the Socket.Blocking property (except in reverse).
        /// Even if this is false, the underlying native socket may still be non-blocking if anything ever caused it to become non-blocking,
        /// either by issuing an async operation or explicitly setting this property to true.
        /// </summary>
        internal bool IsNonBlocking
        {
            get
            {
                return (_flags & Flags.NonBlocking) != 0;
            }
            set
            {
                SetFlag(Flags.NonBlocking, value);

                // If transitioning from blocking to non-blocking, we need to set the native socket to non-blocking mode.
                // If transitioning from non-blocking to blocking, we keep the native socket in non-blocking mode, and emulate
                // blocking operations within SocketAsyncContext on top of epoll/kqueue.
                // This avoids problems with switching to native blocking while there are pending operations.
                if (value)
                {
                    AsyncContext.SetHandleNonBlocking();
                }
            }
        }

        internal bool IsUnderlyingHandleBlocking => !AsyncContext.IsHandleNonBlocking;

        internal int ReceiveTimeout
        {
            get
            {
                return _receiveTimeout;
            }
            set
            {
                Debug.Assert(value == -1 || value > 0, $"Unexpected value: {value}");
                _receiveTimeout = value;
            }
        }

        internal int SendTimeout
        {
            get
            {
                return _sendTimeout;
            }
            set
            {
                Debug.Assert(value == -1 || value > 0, $"Unexpected value: {value}");
                _sendTimeout = value;
            }
        }

        internal bool IsDisconnected => (_flags & Flags.IsDisconnected) != 0;

        internal void SetToDisconnected() => _flags |= Flags.IsDisconnected;

        /// <returns>Returns whether operations were canceled.</returns>
        private bool OnHandleClose()
        {
            // If we've aborted async operations, return true to cause an abortive close.
            return _asyncContext?.StopAndAbort() ?? false;
        }

        /// <returns>Returns whether operations were canceled.</returns>
        private unsafe bool TryUnblockSocket(bool abortive)
        {
            // Calling 'close' on a socket that has pending blocking calls (e.g. recv, send, accept, ...)
            // may block indefinitely. This is a best-effort attempt to not get blocked and make those operations return.
            // We need to ensure we keep the expected TCP behavior that is observed by the socket peer (FIN vs RST close).
            // What we do here isn't specified by POSIX and doesn't work on all OSes.
            // On Linux this works well.
            // On OSX, TCP connections will be closed with a FIN close instead of an abortive RST close.
            // And, pending TCP connect operations and UDP receive are not abortable.

            // Don't disconnect sockets we don't own.
            if (!OwnsHandle)
            {
                return false;
            }

            // Unless we're doing an abortive close, don't touch sockets which don't have the CLOEXEC flag set.
            // These may be shared with other processes and we want to avoid disconnecting them.
            if (!abortive)
            {
                int fdFlags = Interop.Sys.Fcntl.GetFD(handle);
                if (fdFlags == 0)
                {
                    return false;
                }
            }

            int type = 0;
            int optLen = sizeof(int);
            Interop.Error err = Interop.Sys.GetSockOpt(handle, SocketOptionLevel.Socket, SocketOptionName.Type, (byte*)&type, &optLen);
            if (err == Interop.Error.SUCCESS)
            {
                // For TCP (SocketType.Stream), perform an abortive close.
                // Unless the user requested a normal close using Socket.Shutdown.
                if (type == (int)SocketType.Stream && !_hasShutdownSend)
                {
                    Interop.Sys.Disconnect(handle);
                }
                else
                {
                    Interop.Sys.Shutdown(handle, SocketShutdown.Both);
                }
            }

            return true;
        }

        private unsafe SocketError DoCloseHandle(bool abortive)
        {
            Interop.Error errorCode = Interop.Error.SUCCESS;

            if (!IsSocket)
            {
                return SocketPal.GetSocketErrorForErrorCode(CloseHandle(handle));
            }
            if (OperatingSystem.IsWasi())
            {
                // WASI never blocks and doesn't support linger options
                return SocketPal.GetSocketErrorForErrorCode(CloseHandle(handle));
            }

            // If abortive is not set, we're not running on the finalizer thread, so it's safe to block here.
            // We can honor the linger options set on the socket.  It also means closesocket() might return
            // EWOULDBLOCK, in which case we need to do some recovery.
            if (!abortive)
            {
                if (NetEventSource.Log.IsEnabled()) NetEventSource.Info(this, $"handle:{handle} Following 'non-abortive' branch.");

                // Close, and if its errno is other than EWOULDBLOCK, there's nothing more to do - we either succeeded or failed.
                errorCode = CloseHandle(handle);
                if (errorCode != Interop.Error.EWOULDBLOCK)
                {
                    return SocketPal.GetSocketErrorForErrorCode(errorCode);
                }

                // The socket must be non-blocking with a linger timeout set.
                // We have to set the socket to blocking.
                if (Interop.Sys.Fcntl.DangerousSetIsNonBlocking(handle, 0) == 0)
                {
                    // The socket successfully made blocking; retry the close().
                    return SocketPal.GetSocketErrorForErrorCode(CloseHandle(handle));
                }

                // The socket could not be made blocking; fall through to the regular abortive close.
            }

            // By default or if the non-abortive path failed, set linger timeout to zero to get an abortive close (RST).
            var linger = new Interop.Sys.LingerOption
            {
                OnOff = 1,
                Seconds = 0
            };

            errorCode = Interop.Sys.SetLingerOption(handle, &linger);
#if DEBUG
            _closeSocketLinger = SocketPal.GetSocketErrorForErrorCode(errorCode);
#endif
            if (NetEventSource.Log.IsEnabled()) NetEventSource.Info(this, $"handle:{handle}, setsockopt():{errorCode}");

            switch (errorCode)
            {
                case Interop.Error.SUCCESS:
                case Interop.Error.EINVAL:
                case Interop.Error.ENOPROTOOPT:
                case Interop.Error.ENOTSOCK:
                    errorCode = CloseHandle(handle);
                    break;

                // For other errors, it's too dangerous to try closesocket() - it might block!
            }

            return SocketPal.GetSocketErrorForErrorCode(errorCode);
        }

        private Interop.Error CloseHandle(IntPtr handle)
        {
            Interop.Error errorCode = Interop.Error.SUCCESS;
            bool remappedError = false;

            if (Interop.Sys.Close(handle) != 0)
            {
                errorCode = Interop.Sys.GetLastError();
                if (errorCode == Interop.Error.ECONNRESET)
                {
                    // Some Unix platforms (e.g. FreeBSD) non-compliantly return ECONNRESET from close().
                    // For our purposes, we want to ignore such a "failure" and treat it as success.
                    // In such a case, the file descriptor was still closed and there's no corrective
                    // action to take.
                    errorCode = Interop.Error.SUCCESS;
                    remappedError = true;
                }
            }

            if (NetEventSource.Log.IsEnabled())
            {
                NetEventSource.Info(this, remappedError ?
                    $"handle:{handle}, close():ECONNRESET, but treating it as SUCCESS" :
                    $"handle:{handle}, close():{errorCode}");
            }

#if DEBUG
            _closeSocketResult = SocketPal.GetSocketErrorForErrorCode(errorCode);
#endif

            return errorCode;
        }
    }
}
