// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.Versioning;

namespace System.Net.Http
{
    /// <summary>
    /// Exposes process-wide settings for handlers.
    /// </summary>
    internal static class GlobalHttpSettings
    {
        internal static class DiagnosticsHandler
        {
            [FeatureSwitchDefinition("System.Net.Http.EnableActivityPropagation")]
            public static bool EnableActivityPropagation { get; } = RuntimeSettingParser.QueryRuntimeSettingSwitch(
                "System.Net.Http.EnableActivityPropagation",
                "DOTNET_SYSTEM_NET_HTTP_ENABLEACTIVITYPROPAGATION",
                true);
        }

        internal static class MetricsHandler
        {
            [FeatureSwitchDefinition("System.Diagnostics.Metrics.Meter.IsSupported")]
            public static bool IsGloballyEnabled { get; } = RuntimeSettingParser.QueryRuntimeSettingSwitch(
                "System.Diagnostics.Metrics.Meter.IsSupported",
                true);
        }

        internal static class SocketsHttpHandler
        {
#if !TARGET_BROWSER && !TARGET_WASI
            // Default to allowing HTTP/2, but enable that to be overridden by an
            // AppContext switch, or by an environment variable being set to false/0.
            public static bool AllowHttp2 { get; } = RuntimeSettingParser.QueryRuntimeSettingSwitch(
                "System.Net.Http.SocketsHttpHandler.Http2Support",
                "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_HTTP2SUPPORT",
                true);

            // Default to allowing HTTP/3 on platforms where we have QUIC, but enable that to be overridden by an
            // AppContext switch, or by an environment variable being set to false/0.
            [SupportedOSPlatformGuard("linux")]
            [SupportedOSPlatformGuard("macOS")]
            [SupportedOSPlatformGuard("windows")]
            [FeatureSwitchDefinition("System.Net.SocketsHttpHandler.Http3Support")]
            public static bool AllowHttp3 { get; } = RuntimeSettingParser.QueryRuntimeSettingSwitch(
                "System.Net.SocketsHttpHandler.Http3Support",
                "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_HTTP3SUPPORT",
                (OperatingSystem.IsLinux() && !OperatingSystem.IsAndroid()) || OperatingSystem.IsWindows() || OperatingSystem.IsMacOS());

            // Switch to disable the HTTP/2 dynamic window scaling algorithm. Enabled by default.
            public static bool DisableDynamicHttp2WindowSizing { get; } = RuntimeSettingParser.QueryRuntimeSettingSwitch(
                "System.Net.SocketsHttpHandler.Http2FlowControl.DisableDynamicWindowSizing",
                "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_HTTP2FLOWCONTROL_DISABLEDYNAMICWINDOWSIZING",
                false);

            // The maximum size of the HTTP/2 stream receive window. Defaults to 16 MB.
            public static int MaxHttp2StreamWindowSize { get; } = GetMaxHttp2StreamWindowSize();

            // Defaults to 1.0. Higher values result in shorter window, but slower downloads.
            public static double Http2StreamWindowScaleThresholdMultiplier { get; } = GetHttp2StreamWindowScaleThresholdMultiplier();

            public static int PendingConnectionTimeoutOnRequestCompletion { get; } = RuntimeSettingParser.QueryRuntimeSettingInt32(
                "System.Net.SocketsHttpHandler.PendingConnectionTimeoutOnRequestCompletion",
                "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_PENDINGCONNECTIONTIMEOUTONREQUESTCOMPLETION", 5000);

            public const int DefaultHttp2MaxStreamWindowSize = 16 * 1024 * 1024;
            public const double DefaultHttp2StreamWindowScaleThresholdMultiplier = 1.0;

            private static int GetMaxHttp2StreamWindowSize()
            {
                int value = RuntimeSettingParser.ParseInt32EnvironmentVariableValue(
                    "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_FLOWCONTROL_MAXSTREAMWINDOWSIZE",
                    DefaultHttp2MaxStreamWindowSize);

                // Disallow small values:
                if (value < HttpHandlerDefaults.DefaultInitialHttp2StreamWindowSize)
                {
                    value = HttpHandlerDefaults.DefaultInitialHttp2StreamWindowSize;
                }
                return value;
            }

            private static double GetHttp2StreamWindowScaleThresholdMultiplier()
            {
                double value = RuntimeSettingParser.ParseDoubleEnvironmentVariableValue(
                    "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_FLOWCONTROL_STREAMWINDOWSCALETHRESHOLDMULTIPLIER",
                    DefaultHttp2StreamWindowScaleThresholdMultiplier);

                // Disallow negative values:
                if (value < 0)
                {
                    value = DefaultHttp2StreamWindowScaleThresholdMultiplier;
                }
                return value;
            }
#endif

            public static int MaxConnectionsPerServer { get; } = GetMaxConnectionsPerServer();

            private static int GetMaxConnectionsPerServer()
            {
                int value = RuntimeSettingParser.QueryRuntimeSettingInt32(
                    "System.Net.SocketsHttpHandler.MaxConnectionsPerServer",
                    "DOTNET_SYSTEM_NET_HTTP_SOCKETSHTTPHANDLER_MAXCONNECTIONSPERSERVER",
                    int.MaxValue);

                // Disallow invalid values
                if (value < 1)
                {
                    value = int.MaxValue;
                }
                return value;
            }
        }
    }
}
