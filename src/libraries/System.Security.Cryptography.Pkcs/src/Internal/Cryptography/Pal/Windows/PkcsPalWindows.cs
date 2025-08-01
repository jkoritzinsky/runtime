// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Security.Cryptography;
using System.Security.Cryptography.Pkcs;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using Microsoft.Win32.SafeHandles;
using static Interop.Crypt32;

namespace Internal.Cryptography.Pal.Windows
{
    internal sealed partial class PkcsPalWindows : PkcsPal
    {
        internal PkcsPalWindows()
        {
        }

        public sealed override DecryptorPal Decode(ReadOnlySpan<byte> encodedMessage, out int version, out ContentInfo contentInfo, out AlgorithmIdentifier contentEncryptionAlgorithm, out X509Certificate2Collection originatorCerts, out CryptographicAttributeObjectCollection unprotectedAttributes)
        {
            return DecryptorPalWindows.Decode(encodedMessage, out version, out contentInfo, out contentEncryptionAlgorithm, out originatorCerts, out unprotectedAttributes);
        }

        public sealed override Oid GetEncodedMessageType(ReadOnlySpan<byte> encodedMessage)
        {
            using (SafeCryptMsgHandle hCryptMsg = Interop.Crypt32.CryptMsgOpenToDecode(MsgEncodingType.All, 0, 0, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero))
            {
                if (hCryptMsg == null || hCryptMsg.IsInvalid)
                    throw Marshal.GetLastPInvokeError().ToCryptographicException();

                if (!Interop.Crypt32.CryptMsgUpdate(hCryptMsg, ref MemoryMarshal.GetReference(encodedMessage), encodedMessage.Length, fFinal: true))
                    throw Marshal.GetLastPInvokeError().ToCryptographicException();

                int msgTypeAsInt;
                int cbSize = sizeof(int);
                if (!Interop.Crypt32.CryptMsgGetParam(hCryptMsg, CryptMsgParamType.CMSG_TYPE_PARAM, 0, out msgTypeAsInt, ref cbSize))
                    throw Marshal.GetLastPInvokeError().ToCryptographicException();

                return (CryptMsgType)msgTypeAsInt switch
                {
                    CryptMsgType.CMSG_DATA => Oid.FromOidValue(Oids.Pkcs7Data, OidGroup.ExtensionOrAttribute),
                    CryptMsgType.CMSG_SIGNED => Oid.FromOidValue(Oids.Pkcs7Signed, OidGroup.ExtensionOrAttribute),
                    CryptMsgType.CMSG_ENVELOPED => Oid.FromOidValue(Oids.Pkcs7Enveloped, OidGroup.ExtensionOrAttribute),
                    CryptMsgType.CMSG_SIGNED_AND_ENVELOPED => Oid.FromOidValue(Oids.Pkcs7SignedEnveloped, OidGroup.ExtensionOrAttribute),
                    CryptMsgType.CMSG_HASHED => Oid.FromOidValue(Oids.Pkcs7Hashed, OidGroup.ExtensionOrAttribute),
                    CryptMsgType.CMSG_ENCRYPTED => Oid.FromOidValue(Oids.Pkcs7Encrypted, OidGroup.ExtensionOrAttribute),
                    _ => throw ErrorCode.CRYPT_E_INVALID_MSG_TYPE.ToCryptographicException(),
                };
            }
        }

        public sealed override void AddCertsFromStoreForDecryption(X509Certificate2Collection certs)
        {
            certs.AddRange(PkcsHelpers.GetStoreCertificates(StoreName.My, StoreLocation.CurrentUser, openExistingOnly: true));
            certs.AddRange(PkcsHelpers.GetStoreCertificates(StoreName.My, StoreLocation.LocalMachine, openExistingOnly: true));
        }

        public sealed override Exception CreateRecipientsNotFoundException()
        {
            return ErrorCode.CRYPT_E_RECIPIENT_NOT_FOUND.ToCryptographicException();
        }

        public sealed override Exception CreateRecipientInfosAfterEncryptException()
        {
            return ErrorCode.CRYPT_E_INVALID_MSG_TYPE.ToCryptographicException();
        }

        public sealed override Exception CreateDecryptAfterEncryptException()
        {
            return ErrorCode.CRYPT_E_INVALID_MSG_TYPE.ToCryptographicException();
        }

        public sealed override Exception CreateDecryptTwiceException()
        {
            return ErrorCode.CRYPT_E_INVALID_MSG_TYPE.ToCryptographicException();
        }

        public sealed override byte[] GetSubjectKeyIdentifier(X509Certificate2 certificate)
        {
            using (SafeCertContextHandle hCertContext = certificate.CreateCertContextHandle())
            {
                byte[] ski = hCertContext.GetSubjectKeyIdentifier();
                return ski;
            }
        }

        public override T? GetPrivateKeyForSigning<T>(X509Certificate2 certificate, bool silent) where T : class
        {
            return GetPrivateKey<T>(certificate, silent, preferNCrypt: true);
        }

        public override T? GetPrivateKeyForDecryption<T>(X509Certificate2 certificate, bool silent) where T : class
        {
            return GetPrivateKey<T>(certificate, silent, preferNCrypt: false);
        }

        private static T? GetPrivateKey<T>(X509Certificate2 certificate, bool silent, bool preferNCrypt) where T : class, IDisposable
        {
            if (!certificate.HasPrivateKey)
            {
                return null;
            }

            SafeProvOrNCryptKeyHandle? handle = GetCertificatePrivateKey(
                certificate,
                silent,
                preferNCrypt,
                out CryptKeySpec keySpec,
                out Exception? exception);

            using (handle)
            {
                if (handle == null || handle.IsInvalid)
                {
                    if (exception != null)
                    {
                        throw exception;
                    }

                    return null;
                }

                if (keySpec == CryptKeySpec.CERT_NCRYPT_KEY_SPEC)
                {
#if NETSTANDARD || NET
                    Debug.Assert(RuntimeInformation.IsOSPlatform(OSPlatform.Windows));
#endif

                    using (SafeNCryptKeyHandle keyHandle = new SafeNCryptKeyHandle(handle.DangerousGetHandle(), handle))
                    {
                        CngKeyHandleOpenOptions options = CngKeyHandleOpenOptions.None;
                        byte clrIsEphemeral = 0;
                        Interop.NCrypt.ErrorCode errorCode = Interop.NCrypt.NCryptGetByteProperty(keyHandle, "CLR IsEphemeral", ref clrIsEphemeral, CngPropertyOptions.CustomProperty);

                        if (errorCode == Interop.NCrypt.ErrorCode.ERROR_SUCCESS && clrIsEphemeral == 1)
                        {
                            options |= CngKeyHandleOpenOptions.EphemeralKey;
                        }

                        using (CngKey cngKey = CngKey.Open(keyHandle, options))
                        {
                            if (typeof(T) == typeof(RSA))
                                return (T)(object)new RSACng(cngKey);
                            if (typeof(T) == typeof(ECDsa))
                                return (T)(object)new ECDsaCng(cngKey);
                            if (typeof(T) == typeof(DSA))
                                return (T)(object)new DSACng(cngKey);
                            if (typeof(T) == typeof(MLDsa))
                                return (T)(object)new MLDsaCng(cngKey);
                            if (typeof(T) == typeof(SlhDsa))
                                throw new PlatformNotSupportedException(SR.Format(SR.Cryptography_AlgorithmNotSupported, nameof(SlhDsa)));

                            Debug.Fail($"Unknown CNG key type request: {typeof(T).FullName}");
                            return null;
                        }
                    }
                }

                // The key handle is for CAPI.
                // Our CAPI types don't allow usage from a handle, so we have a few choices:
                // 1) Extract the information we need to re-open the key handle.
                // 2) Re-implement {R|D}SACryptoServiceProvider
                // 3) PNSE.
                // 4) Defer to cert.Get{R|D}SAPrivateKey if not silent, throw otherwise.
                CspParameters cspParams = handle.GetProvParameters();
                Debug.Assert(RuntimeInformation.IsOSPlatform(OSPlatform.Windows));
                Debug.Assert((cspParams.Flags & CspProviderFlags.UseExistingKey) != 0);
                cspParams.KeyNumber = (int)keySpec;

                if (silent)
                {
                    cspParams.Flags |= CspProviderFlags.NoPrompt;
                }

                if (typeof(T) == typeof(RSA))
                    return (T)(object)new RSACryptoServiceProvider(cspParams);
                if (typeof(T) == typeof(DSA))
                    return (T)(object)new DSACryptoServiceProvider(cspParams);

                Debug.Fail($"Unknown CAPI key type request: {typeof(T).FullName}");
                return null;
            }
        }

        internal static SafeProvOrNCryptKeyHandle? GetCertificatePrivateKey(
            X509Certificate2 cert,
            bool silent,
            bool preferNCrypt,
            out CryptKeySpec keySpec,
            out Exception? exception)
        {
            CryptAcquireCertificatePrivateKeyFlags flags =
                CryptAcquireCertificatePrivateKeyFlags.CRYPT_ACQUIRE_USE_PROV_INFO_FLAG
                | CryptAcquireCertificatePrivateKeyFlags.CRYPT_ACQUIRE_COMPARE_KEY_FLAG;

            if (preferNCrypt)
            {
                flags |= CryptAcquireCertificatePrivateKeyFlags.CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG;
            }
            else
            {
                flags |= CryptAcquireCertificatePrivateKeyFlags.CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG;
            }

            if (silent)
            {
                flags |= CryptAcquireCertificatePrivateKeyFlags.CRYPT_ACQUIRE_SILENT_FLAG;
            }

            bool isNCrypt;
            bool mustFree;
            using (SafeCertContextHandle hCertContext = cert.CreateCertContextHandle())
            {
                IntPtr hKey;
                int cbSize = IntPtr.Size;

                if (Interop.Crypt32.CertGetCertificateContextProperty(
                    hCertContext,
                    CertContextPropId.CERT_NCRYPT_KEY_HANDLE_PROP_ID,
                    out hKey,
                    ref cbSize))
                {
                    exception = null;
                    keySpec = CryptKeySpec.CERT_NCRYPT_KEY_SPEC;
                    return new SafeProvOrNCryptKeyHandleUwp(hKey, hCertContext);
                }

                if (!Interop.Crypt32.CryptAcquireCertificatePrivateKey(
                    hCertContext,
                    flags,
                    IntPtr.Zero,
                    out hKey,
                    out keySpec,
                    out mustFree))
                {
                    exception = Marshal.GetHRForLastWin32Error().ToCryptographicException();
                    return null;
                }

                // We need to know whether we got back a CRYPTPROV or NCrypt handle.
                // Unfortunately, NCryptIsKeyHandle() is a prohibited api on UWP.
                // Fortunately, CryptAcquireCertificatePrivateKey() is documented to tell us which
                // one we got through the keySpec value.
                switch (keySpec)
                {
                    case CryptKeySpec.AT_KEYEXCHANGE:
                    case CryptKeySpec.AT_SIGNATURE:
                        isNCrypt = false;
                        break;

                    case CryptKeySpec.CERT_NCRYPT_KEY_SPEC:
                        isNCrypt = true;
                        break;

                    default:
                        // As of this writing, we've exhausted all the known values of keySpec.
                        // We have no idea what kind of key handle we got so play it safe and fail fast.
                        throw new NotSupportedException(SR.Format(SR.Cryptography_Cms_UnknownKeySpec, keySpec));
                }

                SafeProvOrNCryptKeyHandleUwp hProvOrNCryptKey = new SafeProvOrNCryptKeyHandleUwp(
                    hKey,
                    ownsHandle: mustFree,
                    isNcrypt: isNCrypt);

                exception = null;
                return hProvOrNCryptKey;
            }
        }
    }
}
