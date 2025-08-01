// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Buffers.Binary;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace System.Reflection
{
    internal sealed class RuntimeCustomAttributeData : CustomAttributeData
    {
        #region Internal Static Members
        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeType target)
        {
            Debug.Assert(target is not null);

            IList<CustomAttributeData> cad = GetCustomAttributes(target.GetRuntimeModule(), target.MetadataToken);
            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(target, (RuntimeType)typeof(object), ref pcas);
            return pcas.Count > 0 ? GetCombinedList(cad, ref pcas) : cad;
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeFieldInfo target)
        {
            Debug.Assert(target is not null);

            IList<CustomAttributeData> cad = GetCustomAttributes(target.GetRuntimeModule(), target.MetadataToken);
            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(target, (RuntimeType)typeof(object), ref pcas);
            return pcas.Count > 0 ? GetCombinedList(cad, ref pcas) : cad;
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeMethodInfo target)
        {
            Debug.Assert(target is not null);

            IList<CustomAttributeData> cad = GetCustomAttributes(target.GetRuntimeModule(), target.MetadataToken);
            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(target, (RuntimeType)typeof(object), ref pcas);
            return pcas.Count > 0 ? GetCombinedList(cad, ref pcas) : cad;
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeConstructorInfo target)
        {
            Debug.Assert(target is not null);

            return GetCustomAttributes(target.GetRuntimeModule(), target.MetadataToken);
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeEventInfo target)
        {
            Debug.Assert(target is not null);

            return GetCustomAttributes(target.GetRuntimeModule(), target.MetadataToken);
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimePropertyInfo target)
        {
            Debug.Assert(target is not null);

            return GetCustomAttributes(target.GetRuntimeModule(), target.MetadataToken);
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeModule target)
        {
            Debug.Assert(target is not null);

            if (target.IsResource())
                return new List<CustomAttributeData>();

            return GetCustomAttributes(target, target.MetadataToken);
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeAssembly target)
        {
            Debug.Assert(target is not null);

            // No pseudo attributes for RuntimeAssembly

            return GetCustomAttributes((RuntimeModule)target.ManifestModule, RuntimeAssembly.GetToken(target));
        }

        internal static IList<CustomAttributeData> GetCustomAttributesInternal(RuntimeParameterInfo target)
        {
            Debug.Assert(target is not null);

            RuntimeType.ListBuilder<Attribute> pcas = default;
            IList<CustomAttributeData> cad = GetCustomAttributes(target.GetRuntimeModule()!, target.MetadataToken);
            PseudoCustomAttribute.GetCustomAttributes(target, (RuntimeType)typeof(object), ref pcas);
            return pcas.Count > 0 ? GetCombinedList(cad, ref pcas) : cad;
        }

        private static ReadOnlyCollection<CustomAttributeData> GetCombinedList(IList<CustomAttributeData> customAttributes, ref RuntimeType.ListBuilder<Attribute> pseudoAttributes)
        {
            Debug.Assert(pseudoAttributes.Count != 0);

            CustomAttributeData[] pca = new CustomAttributeData[customAttributes.Count + pseudoAttributes.Count];
            customAttributes.CopyTo(pca, pseudoAttributes.Count);
            for (int i = 0; i < pseudoAttributes.Count; i++)
            {
                pca[i] = new RuntimeCustomAttributeData(pseudoAttributes[i]);
            }

            return Array.AsReadOnly(pca);
        }
        #endregion

        internal static CustomAttributeEncoding TypeToCustomAttributeEncoding(RuntimeType type)
        {
            if (type == typeof(int))
                return CustomAttributeEncoding.Int32;

            if (type.IsActualEnum)
                return CustomAttributeEncoding.Enum;

            if (type == typeof(string))
                return CustomAttributeEncoding.String;

            if (type == typeof(Type))
                return CustomAttributeEncoding.Type;

            if (type == typeof(object))
                return CustomAttributeEncoding.Object;

            if (type.IsArray)
                return CustomAttributeEncoding.Array;

            if (type == typeof(char))
                return CustomAttributeEncoding.Char;

            if (type == typeof(bool))
                return CustomAttributeEncoding.Boolean;

            if (type == typeof(byte))
                return CustomAttributeEncoding.Byte;

            if (type == typeof(sbyte))
                return CustomAttributeEncoding.SByte;

            if (type == typeof(short))
                return CustomAttributeEncoding.Int16;

            if (type == typeof(ushort))
                return CustomAttributeEncoding.UInt16;

            if (type == typeof(uint))
                return CustomAttributeEncoding.UInt32;

            if (type == typeof(long))
                return CustomAttributeEncoding.Int64;

            if (type == typeof(ulong))
                return CustomAttributeEncoding.UInt64;

            if (type == typeof(float))
                return CustomAttributeEncoding.Float;

            if (type == typeof(double))
                return CustomAttributeEncoding.Double;

            // System.Enum is neither an Enum nor a Class
            if (type == typeof(Enum))
                return CustomAttributeEncoding.Object;

            if (type.IsClass)
                return CustomAttributeEncoding.Object;

            if (type.IsInterface)
                return CustomAttributeEncoding.Object;

            if (type.IsActualValueType)
                return CustomAttributeEncoding.Undefined;

            throw new ArgumentException(SR.Argument_InvalidKindOfTypeForCA, nameof(type));
        }

        #region Private Static Methods
        private static IList<CustomAttributeData> GetCustomAttributes(RuntimeModule module, int tkTarget)
        {
            CustomAttributeRecord[] records = GetCustomAttributeRecords(module, tkTarget);
            if (records.Length == 0)
            {
                return Array.Empty<CustomAttributeData>();
            }

            CustomAttributeData[] customAttributes = new CustomAttributeData[records.Length];
            for (int i = 0; i < records.Length; i++)
                customAttributes[i] = new RuntimeCustomAttributeData(module, records[i].tkCtor, in records[i].blob);

            return Array.AsReadOnly(customAttributes);
        }
        #endregion

        #region Internal Static Members
        internal static CustomAttributeRecord[] GetCustomAttributeRecords(RuntimeModule module, int targetToken)
        {
            MetadataImport scope = module.MetadataImport;

            scope.EnumCustomAttributes(targetToken, out MetadataEnumResult tkCustomAttributeTokens);

            if (tkCustomAttributeTokens.Length == 0)
            {
                return Array.Empty<CustomAttributeRecord>();
            }

            CustomAttributeRecord[] records = new CustomAttributeRecord[tkCustomAttributeTokens.Length];

            for (int i = 0; i < records.Length; i++)
            {
                scope.GetCustomAttributeProps(tkCustomAttributeTokens[i],
                    out records[i].tkCtor.Value, out records[i].blob);
            }
            GC.KeepAlive(module);

            return records;
        }

        internal static CustomAttributeTypedArgument Filter(IList<CustomAttributeData> attrs, Type? caType, int parameter)
        {
            for (int i = 0; i < attrs.Count; i++)
            {
                if (attrs[i].Constructor.DeclaringType == caType)
                {
                    return attrs[i].ConstructorArguments[parameter];
                }
            }

            return default;
        }
        #endregion

        private ConstructorInfo m_ctor = null!;
        private readonly RuntimeModule m_scope = null!;
        private readonly CustomAttributeCtorParameter[] m_ctorParams = null!;
        private readonly CustomAttributeNamedParameter[] m_namedParams = null!;
        private IList<CustomAttributeTypedArgument> m_typedCtorArgs = null!;
        private IList<CustomAttributeNamedArgument> m_namedArgs = null!;

        #region Constructor
        [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2075:UnrecognizedReflectionPattern",
            Justification = "Property setters and fields which are accessed by any attribute instantiation which is present in the code linker has analyzed." +
                            "As such enumerating all fields and properties may return different results after trimming" +
                            "but all those which are needed to actually have data will be there.")]
        [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2026:UnrecognizedReflectionPattern",
            Justification = "We're getting a MethodBase of a constructor that we found in the metadata. The attribute constructor won't be trimmed.")]
        private RuntimeCustomAttributeData(RuntimeModule scope, MetadataToken caCtorToken, in ConstArray blob)
        {
            m_scope = scope;
            m_ctor = (RuntimeConstructorInfo)RuntimeType.GetMethodBase(m_scope, caCtorToken)!;

            if (m_ctor!.DeclaringType!.IsGenericType)
            {
                MetadataImport metadataScope = m_scope.MetadataImport;
                Type attributeType = m_scope.ResolveType(metadataScope.GetParentToken(caCtorToken), null, null)!;
                m_ctor = (RuntimeConstructorInfo)m_scope.ResolveMethod(caCtorToken, attributeType.GenericTypeArguments, null)!.MethodHandle.GetMethodInfo();
            }

            ReadOnlySpan<ParameterInfo> parameters = m_ctor.GetParametersAsSpan();
            if (parameters.Length != 0)
            {
                m_ctorParams = new CustomAttributeCtorParameter[parameters.Length];
                for (int i = 0; i < parameters.Length; i++)
                    m_ctorParams[i] = new CustomAttributeCtorParameter(new CustomAttributeType((RuntimeType)parameters[i].ParameterType));
            }
            else
            {
                m_ctorParams = Array.Empty<CustomAttributeCtorParameter>();
            }

            FieldInfo[] fields = m_ctor.DeclaringType!.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            PropertyInfo[] properties = m_ctor.DeclaringType.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);

            // Allocate collections for members and names params.
            m_namedParams = new CustomAttributeNamedParameter[properties.Length + fields.Length];

            int idx = 0;
            foreach (FieldInfo fi in fields)
            {
                m_namedParams[idx++] = new CustomAttributeNamedParameter(
                    fi,
                    CustomAttributeEncoding.Field,
                    new CustomAttributeType((RuntimeType)fi.FieldType));
            }

            foreach (PropertyInfo pi in properties)
            {
                m_namedParams[idx++] = new CustomAttributeNamedParameter(
                    pi,
                    CustomAttributeEncoding.Property,
                    new CustomAttributeType((RuntimeType)pi.PropertyType));
            }

            CustomAttributeEncodedArgument.ParseAttributeArguments(blob, m_ctorParams, m_namedParams, m_scope);
        }
        #endregion

        #region Pseudo Custom Attribute Constructor
        internal RuntimeCustomAttributeData(Attribute attribute)
        {
           if (attribute is DllImportAttribute dllImportAttribute)
               Init(dllImportAttribute);
           else if (attribute is FieldOffsetAttribute fieldOffsetAttribute)
               Init(fieldOffsetAttribute);
           else if (attribute is MarshalAsAttribute marshalAsAttribute)
               Init(marshalAsAttribute);
           else if (attribute is TypeForwardedToAttribute typeForwardedToAttribute)
               Init(typeForwardedToAttribute);
           else
               Init(attribute);
        }
        private void Init(DllImportAttribute dllImport)
        {
            Type type = typeof(DllImportAttribute);
            m_ctor = type.GetConstructors(BindingFlags.Public | BindingFlags.Instance)[0];
            m_typedCtorArgs = Array.AsReadOnly(new CustomAttributeTypedArgument[]
            {
                new CustomAttributeTypedArgument(dllImport.Value),
            });

            m_namedArgs = Array.AsReadOnly(new CustomAttributeNamedArgument[]
            {
                new CustomAttributeNamedArgument(type.GetField("EntryPoint")!, dllImport.EntryPoint),
                new CustomAttributeNamedArgument(type.GetField("CharSet")!, dllImport.CharSet),
                new CustomAttributeNamedArgument(type.GetField("ExactSpelling")!, dllImport.ExactSpelling),
                new CustomAttributeNamedArgument(type.GetField("SetLastError")!, dllImport.SetLastError),
                new CustomAttributeNamedArgument(type.GetField("PreserveSig")!, dllImport.PreserveSig),
                new CustomAttributeNamedArgument(type.GetField("CallingConvention")!, dllImport.CallingConvention),
                new CustomAttributeNamedArgument(type.GetField("BestFitMapping")!, dllImport.BestFitMapping),
                new CustomAttributeNamedArgument(type.GetField("ThrowOnUnmappableChar")!, dllImport.ThrowOnUnmappableChar)
            });
        }
        private void Init(FieldOffsetAttribute fieldOffset)
        {
            m_ctor = typeof(FieldOffsetAttribute).GetConstructors(BindingFlags.Public | BindingFlags.Instance)[0];
            m_typedCtorArgs = Array.AsReadOnly(new CustomAttributeTypedArgument[] {
                new CustomAttributeTypedArgument(fieldOffset.Value)
            });
            m_namedArgs = Array.Empty<CustomAttributeNamedArgument>();
        }
        private void Init(MarshalAsAttribute marshalAs)
        {
            Type type = typeof(MarshalAsAttribute);
            m_ctor = type.GetConstructors(BindingFlags.Public | BindingFlags.Instance)[0];
            m_typedCtorArgs = Array.AsReadOnly(new CustomAttributeTypedArgument[]
            {
                new CustomAttributeTypedArgument(marshalAs.Value),
            });

            int i = 3; // ArraySubType, SizeParamIndex, SizeConst
            if (marshalAs.MarshalType is not null) i++;
            if (marshalAs.MarshalTypeRef is not null) i++;
            if (marshalAs.MarshalCookie is not null) i++;
            i++; // IidParameterIndex
            i++; // SafeArraySubType
            if (marshalAs.SafeArrayUserDefinedSubType is not null) i++;
            CustomAttributeNamedArgument[] namedArgs = new CustomAttributeNamedArgument[i];

            // For compatibility with previous runtimes, we always include the following 5 attributes, regardless
            // of if they apply to the UnmanagedType being marshaled or not.
            i = 0;
            namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("ArraySubType")!, marshalAs.ArraySubType);
            namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("SizeParamIndex")!, marshalAs.SizeParamIndex);
            namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("SizeConst")!, marshalAs.SizeConst);
            namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("IidParameterIndex")!, marshalAs.IidParameterIndex);
            namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("SafeArraySubType")!, marshalAs.SafeArraySubType);
            if (marshalAs.MarshalType is not null)
                namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("MarshalType")!, marshalAs.MarshalType);
            if (marshalAs.MarshalTypeRef is not null)
                namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("MarshalTypeRef")!, marshalAs.MarshalTypeRef);
            if (marshalAs.MarshalCookie is not null)
                namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("MarshalCookie")!, marshalAs.MarshalCookie);
            if (marshalAs.SafeArrayUserDefinedSubType is not null)
                namedArgs[i++] = new CustomAttributeNamedArgument(type.GetField("SafeArrayUserDefinedSubType")!, marshalAs.SafeArrayUserDefinedSubType);

            m_namedArgs = Array.AsReadOnly(namedArgs);
        }
        private void Init(TypeForwardedToAttribute forwardedTo)
        {
            Type type = typeof(TypeForwardedToAttribute);

            Type[] sig = [typeof(Type)];
            m_ctor = type.GetConstructor(BindingFlags.Public | BindingFlags.Instance, null, sig, null)!;

            CustomAttributeTypedArgument[] typedArgs = [new CustomAttributeTypedArgument(typeof(Type), forwardedTo.Destination)];
            m_typedCtorArgs = Array.AsReadOnly(typedArgs);

            m_namedArgs = Array.Empty<CustomAttributeNamedArgument>();
        }

        [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2075:UnrecognizedReflectionPattern",
            Justification = "The pca object had to be created by the single ctor on the Type. So the ctor couldn't have been trimmed.")]
        private void Init(object pca)
        {
            Type type = pca.GetType();

#if DEBUG
            // Ensure there is only a single constructor for 'pca', so it is safe to suppress IL2075
            ConstructorInfo[] allCtors = type.GetConstructors(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);
            Debug.Assert(allCtors.Length == 1);
            Debug.Assert(allCtors[0].GetParametersAsSpan().Length == 0);
#endif

            m_ctor = type.GetConstructors(BindingFlags.Public | BindingFlags.Instance)[0];
            m_typedCtorArgs = Array.Empty<CustomAttributeTypedArgument>();
            m_namedArgs = Array.Empty<CustomAttributeNamedArgument>();
        }
        #endregion

        #region Public Members
        public override ConstructorInfo Constructor => m_ctor;

        public override IList<CustomAttributeTypedArgument> ConstructorArguments
        {
            get
            {
                if (m_typedCtorArgs is null)
                {
                    if (m_ctorParams.Length != 0)
                    {
                        CustomAttributeTypedArgument[] typedCtorArgs = new CustomAttributeTypedArgument[m_ctorParams.Length];

                        for (int i = 0; i < typedCtorArgs.Length; i++)
                        {
                            CustomAttributeEncodedArgument encodedArg = m_ctorParams[i].EncodedArgument!;

                            typedCtorArgs[i] = new CustomAttributeTypedArgument(m_scope, encodedArg);
                        }

                        m_typedCtorArgs = Array.AsReadOnly(typedCtorArgs);
                    }
                    else
                    {
                        m_typedCtorArgs = Array.Empty<CustomAttributeTypedArgument>();
                    }
                }

                return m_typedCtorArgs;
            }
        }

        public override IList<CustomAttributeNamedArgument> NamedArguments
        {
            get
            {
                if (m_namedArgs is null)
                {
                    int cNamedArgs = 0;
                    if (m_namedParams is not null)
                    {
                        foreach (CustomAttributeNamedParameter p in m_namedParams)
                        {
                            if (p.EncodedArgument is not null
                                && p.EncodedArgument.CustomAttributeType.EncodedType != CustomAttributeEncoding.Undefined)
                            {
                                cNamedArgs++;
                            }
                        }
                    }

                    if (cNamedArgs != 0)
                    {
                        CustomAttributeNamedArgument[] namedArgs = new CustomAttributeNamedArgument[cNamedArgs];

                        int j = 0;
                        foreach (CustomAttributeNamedParameter p in m_namedParams!)
                        {
                            if (p.EncodedArgument is not null
                                && p.EncodedArgument.CustomAttributeType.EncodedType != CustomAttributeEncoding.Undefined)
                            {
                                Debug.Assert(p.MemberInfo is not null);
                                namedArgs[j++] = new CustomAttributeNamedArgument(
                                    p.MemberInfo,
                                    new CustomAttributeTypedArgument(m_scope, p.EncodedArgument));
                            }
                        }

                        m_namedArgs = Array.AsReadOnly(namedArgs);
                    }
                    else
                    {
                        m_namedArgs = Array.Empty<CustomAttributeNamedArgument>();
                    }
                }

                return m_namedArgs;
            }
        }
        #endregion
    }

    public readonly partial struct CustomAttributeTypedArgument
    {
        #region Private Static Methods
        private static Type CustomAttributeEncodingToType(CustomAttributeEncoding encodedType)
        {
            return encodedType switch
            {
                CustomAttributeEncoding.Enum => typeof(Enum),
                CustomAttributeEncoding.Int32 => typeof(int),
                CustomAttributeEncoding.String => typeof(string),
                CustomAttributeEncoding.Type => typeof(Type),
                CustomAttributeEncoding.Array => typeof(Array),
                CustomAttributeEncoding.Char => typeof(char),
                CustomAttributeEncoding.Boolean => typeof(bool),
                CustomAttributeEncoding.SByte => typeof(sbyte),
                CustomAttributeEncoding.Byte => typeof(byte),
                CustomAttributeEncoding.Int16 => typeof(short),
                CustomAttributeEncoding.UInt16 => typeof(ushort),
                CustomAttributeEncoding.UInt32 => typeof(uint),
                CustomAttributeEncoding.Int64 => typeof(long),
                CustomAttributeEncoding.UInt64 => typeof(ulong),
                CustomAttributeEncoding.Float => typeof(float),
                CustomAttributeEncoding.Double => typeof(double),
                CustomAttributeEncoding.Object => typeof(object),
                _ => throw new ArgumentException(SR.Format(SR.Arg_EnumIllegalVal, (int)encodedType), nameof(encodedType)),
            };
        }

        private static object EncodedValueToRawValue(PrimitiveValue val, CustomAttributeEncoding encodedType)
        {
            return encodedType switch
            {
                CustomAttributeEncoding.Boolean => (byte)val.Byte4 != 0,
                CustomAttributeEncoding.Char => (char)val.Byte4,
                CustomAttributeEncoding.Byte => (byte)val.Byte4,
                CustomAttributeEncoding.SByte => (sbyte)val.Byte4,
                CustomAttributeEncoding.Int16 => (short)val.Byte4,
                CustomAttributeEncoding.UInt16 => (ushort)val.Byte4,
                CustomAttributeEncoding.Int32 => val.Byte4,
                CustomAttributeEncoding.UInt32 => (uint)val.Byte4,
                CustomAttributeEncoding.Int64 => val.Byte8,
                CustomAttributeEncoding.UInt64 => (ulong)val.Byte8,
                CustomAttributeEncoding.Float => BitConverter.Int32BitsToSingle(val.Byte4),
                CustomAttributeEncoding.Double => BitConverter.Int64BitsToDouble(val.Byte8),
                _ => throw new ArgumentException(SR.Format(SR.Arg_EnumIllegalVal, val.Byte8), nameof(val))
            };
        }
        private static RuntimeType ResolveType(RuntimeModule scope, string typeName)
        {
            RuntimeType type = TypeNameResolver.GetTypeReferencedByCustomAttribute(typeName, scope);
            Debug.Assert(type is not null);
            return type;
        }
        #endregion

        internal CustomAttributeTypedArgument(RuntimeModule scope, CustomAttributeEncodedArgument encodedArg)
        {
            CustomAttributeEncoding encodedType = encodedArg.CustomAttributeType.EncodedType;

            if (encodedType == CustomAttributeEncoding.Undefined)
                throw new ArgumentException(null, nameof(encodedArg));

            if (encodedType == CustomAttributeEncoding.Enum)
            {
                _argumentType = encodedArg.CustomAttributeType.EnumType!;
                _value = EncodedValueToRawValue(encodedArg.PrimitiveValue, encodedArg.CustomAttributeType.EncodedEnumType);
            }
            else if (encodedType == CustomAttributeEncoding.String)
            {
                _argumentType = typeof(string);
                _value = encodedArg.StringValue;
            }
            else if (encodedType == CustomAttributeEncoding.Type)
            {
                _argumentType = typeof(Type);

                _value = null;

                if (encodedArg.StringValue is not null)
                    _value = ResolveType(scope, encodedArg.StringValue);
            }
            else if (encodedType == CustomAttributeEncoding.Array)
            {
                encodedType = encodedArg.CustomAttributeType.EncodedArrayType;
                Type elementType;

                if (encodedType == CustomAttributeEncoding.Enum)
                {
                    elementType = encodedArg.CustomAttributeType.EnumType!;
                }
                else
                {
                    elementType = CustomAttributeEncodingToType(encodedType);
                }

                _argumentType = elementType.MakeArrayType();

                if (encodedArg.ArrayValue is null)
                {
                    _value = null;
                }
                else
                {
                    CustomAttributeTypedArgument[] arrayValue = new CustomAttributeTypedArgument[encodedArg.ArrayValue.Length];
                    for (int i = 0; i < arrayValue.Length; i++)
                        arrayValue[i] = new CustomAttributeTypedArgument(scope, encodedArg.ArrayValue[i]);

                    _value = Array.AsReadOnly(arrayValue);
                }
            }
            else
            {
                _argumentType = CustomAttributeEncodingToType(encodedType);
                _value = EncodedValueToRawValue(encodedArg.PrimitiveValue, encodedType);
            }
        }
    }

    internal struct CustomAttributeRecord
    {
        internal ConstArray blob;
        internal MetadataToken tkCtor;

        public CustomAttributeRecord(int token, ConstArray blob)
        {
            tkCtor = new MetadataToken(token);
            this.blob = blob;
        }
    }

    // See CorSerializationType in corhdr.h
    internal enum CustomAttributeEncoding : int
    {
        Undefined = 0,
        Boolean = CorElementType.ELEMENT_TYPE_BOOLEAN,
        Char = CorElementType.ELEMENT_TYPE_CHAR,
        SByte = CorElementType.ELEMENT_TYPE_I1,
        Byte = CorElementType.ELEMENT_TYPE_U1,
        Int16 = CorElementType.ELEMENT_TYPE_I2,
        UInt16 = CorElementType.ELEMENT_TYPE_U2,
        Int32 = CorElementType.ELEMENT_TYPE_I4,
        UInt32 = CorElementType.ELEMENT_TYPE_U4,
        Int64 = CorElementType.ELEMENT_TYPE_I8,
        UInt64 = CorElementType.ELEMENT_TYPE_U8,
        Float = CorElementType.ELEMENT_TYPE_R4,
        Double = CorElementType.ELEMENT_TYPE_R8,
        String = CorElementType.ELEMENT_TYPE_STRING,
        Array = CorElementType.ELEMENT_TYPE_SZARRAY,
        Type = 0x50,
        Object = 0x51,
        Field = 0x53,
        Property = 0x54,
        Enum = 0x55
    }

    [StructLayout(LayoutKind.Explicit)]
    internal struct PrimitiveValue
    {
        [FieldOffset(0)]
        public int Byte4;

        [FieldOffset(0)]
        public long Byte8;
    }

    internal sealed class CustomAttributeEncodedArgument
    {
        internal static void ParseAttributeArguments(
            ConstArray attributeBlob,
            CustomAttributeCtorParameter[] customAttributeCtorParameters,
            CustomAttributeNamedParameter[] customAttributeNamedParameters,
            RuntimeModule customAttributeModule)
        {
            ArgumentNullException.ThrowIfNull(customAttributeModule);

            Debug.Assert(customAttributeCtorParameters is not null);
            Debug.Assert(customAttributeNamedParameters is not null);

            if (customAttributeCtorParameters.Length != 0 || customAttributeNamedParameters.Length != 0)
            {
                CustomAttributeDataParser parser = new CustomAttributeDataParser(attributeBlob);
                try
                {
                    if (!parser.ValidateProlog())
                    {
                        throw new BadImageFormatException(SR.Arg_CustomAttributeFormatException);
                    }

                    ParseCtorArgs(ref parser, customAttributeCtorParameters, customAttributeModule);
                    ParseNamedArgs(ref parser, customAttributeNamedParameters, customAttributeModule);
                }
                catch (Exception ex) when (ex is not OutOfMemoryException)
                {
                    throw new CustomAttributeFormatException(ex.Message, ex);
                }
            }
        }

        internal CustomAttributeEncodedArgument(CustomAttributeType type)
        {
            CustomAttributeType = type;
        }

        public CustomAttributeType CustomAttributeType { get; }
        public PrimitiveValue PrimitiveValue { get; set; }
        public CustomAttributeEncodedArgument[]? ArrayValue { get; set; }
        public string? StringValue { get; set; }

        private static void ParseCtorArgs(
            ref CustomAttributeDataParser parser,
            CustomAttributeCtorParameter[] customAttributeCtorParameters,
            RuntimeModule module)
        {
            foreach (CustomAttributeCtorParameter p in customAttributeCtorParameters)
            {
                p.EncodedArgument = ParseCustomAttributeValue(
                    ref parser,
                    p.CustomAttributeType,
                    module);
            }
        }

        private static void ParseNamedArgs(
            ref CustomAttributeDataParser parser,
            CustomAttributeNamedParameter[] customAttributeNamedParameters,
            RuntimeModule module)
        {
            // Parse the named arguments in the custom attribute.
            int argCount = parser.GetI2();

            for (int i = 0; i < argCount; ++i)
            {
                // Determine if a field or property.
                CustomAttributeEncoding namedArgFieldOrProperty = parser.GetTag();
                if (namedArgFieldOrProperty is not CustomAttributeEncoding.Field
                    && namedArgFieldOrProperty is not CustomAttributeEncoding.Property)
                {
                    throw new BadImageFormatException(SR.Arg_CustomAttributeFormatException);
                }

                // Parse the encoded type for the named argument.
                CustomAttributeType argType = ParseCustomAttributeType(ref parser, module);

                string? argName = parser.GetString();

                // Argument name must be non-null and non-empty.
                if (string.IsNullOrEmpty(argName))
                {
                    throw new BadImageFormatException(SR.Arg_CustomAttributeFormatException);
                }

                // Update the appropriate named argument element.
                CustomAttributeNamedParameter? parameterToUpdate = null;
                foreach (CustomAttributeNamedParameter namedParam in customAttributeNamedParameters)
                {
                    CustomAttributeType namedArgType = namedParam.CustomAttributeType;
                    if (namedArgType.EncodedType != CustomAttributeEncoding.Object)
                    {
                        if (namedArgType.EncodedType != argType.EncodedType)
                        {
                            continue;
                        }

                        // Match array type
                        if (argType.EncodedType is CustomAttributeEncoding.Array
                            && namedArgType.EncodedArrayType is not CustomAttributeEncoding.Object
                            && argType.EncodedArrayType != namedArgType.EncodedArrayType)
                        {
                            continue;
                        }
                    }

                    // Match name
                    if (!namedParam.MemberInfo.Name.Equals(argName))
                    {
                        continue;
                    }

                    // If enum, match enum name.
                    if (namedArgType.EncodedType is CustomAttributeEncoding.Enum
                        || (namedArgType.EncodedType is CustomAttributeEncoding.Array
                            && namedArgType.EncodedArrayType is CustomAttributeEncoding.Enum))
                    {
                        if (!ReferenceEquals(argType.EnumType, namedArgType.EnumType))
                        {
                            continue;
                        }

                        Debug.Assert(namedArgType.EncodedEnumType == argType.EncodedEnumType);
                    }

                    // Found a match
                    parameterToUpdate = namedParam;
                    break;
                }

                if (parameterToUpdate is null)
                {
                    throw new BadImageFormatException(SR.Arg_CustomAttributeUnknownNamedArgument);
                }

                if (parameterToUpdate.EncodedArgument is not null)
                {
                    throw new BadImageFormatException(SR.Arg_CustomAttributeDuplicateNamedArgument);
                }

                parameterToUpdate.EncodedArgument = ParseCustomAttributeValue(ref parser, argType, module);
            }
        }

        private static CustomAttributeEncodedArgument ParseCustomAttributeValue(
            ref CustomAttributeDataParser parser,
            CustomAttributeType type,
            RuntimeModule module)
        {
            CustomAttributeType attributeType = type.EncodedType == CustomAttributeEncoding.Object
                ? ParseCustomAttributeType(ref parser, module)
                : type;

            CustomAttributeEncodedArgument arg = new(attributeType);

            CustomAttributeEncoding underlyingType = attributeType.EncodedType == CustomAttributeEncoding.Enum
                ? attributeType.EncodedEnumType
                : attributeType.EncodedType;

            switch (underlyingType)
            {
                case CustomAttributeEncoding.Boolean:
                case CustomAttributeEncoding.Byte:
                case CustomAttributeEncoding.SByte:
                    arg.PrimitiveValue = new PrimitiveValue() { Byte4 = parser.GetU1() };
                    break;
                case CustomAttributeEncoding.Char:
                case CustomAttributeEncoding.Int16:
                case CustomAttributeEncoding.UInt16:
                    arg.PrimitiveValue = new PrimitiveValue() { Byte4 = parser.GetU2() };
                    break;
                case CustomAttributeEncoding.Int32:
                case CustomAttributeEncoding.UInt32:
                    arg.PrimitiveValue = new PrimitiveValue() { Byte4 = parser.GetI4() };
                    break;
                case CustomAttributeEncoding.Int64:
                case CustomAttributeEncoding.UInt64:
                    arg.PrimitiveValue = new PrimitiveValue() { Byte8 = parser.GetI8() };
                    break;
                case CustomAttributeEncoding.Float:
                    arg.PrimitiveValue = new PrimitiveValue() { Byte4 = BitConverter.SingleToInt32Bits(parser.GetR4()) };
                    break;
                case CustomAttributeEncoding.Double:
                    arg.PrimitiveValue = new PrimitiveValue() { Byte8 = BitConverter.DoubleToInt64Bits(parser.GetR8()) };
                    break;
                case CustomAttributeEncoding.String:
                case CustomAttributeEncoding.Type:
                    arg.StringValue = parser.GetString();
                    break;
                case CustomAttributeEncoding.Array:
                {
                    arg.ArrayValue = null;
                    int len = parser.GetI4();
                    if (len != -1) // indicates array is null - ECMA-335 II.23.3.
                    {
                        attributeType = new CustomAttributeType(
                            attributeType.EncodedArrayType,
                            CustomAttributeEncoding.Undefined, // Array type
                            attributeType.EncodedEnumType,
                            attributeType.EnumType);
                        arg.ArrayValue = new CustomAttributeEncodedArgument[len];
                        for (int i = 0; i < len; ++i)
                        {
                            arg.ArrayValue[i] = ParseCustomAttributeValue(ref parser, attributeType, module);
                        }
                    }
                    break;
                }
                default:
                    throw new BadImageFormatException();
            }

            return arg;
        }

        private static CustomAttributeType ParseCustomAttributeType(ref CustomAttributeDataParser parser, RuntimeModule module)
        {
            CustomAttributeEncoding arrayTag = CustomAttributeEncoding.Undefined;
            CustomAttributeEncoding enumTag = CustomAttributeEncoding.Undefined;
            Type? enumType = null;

            CustomAttributeEncoding tag = parser.GetTag();
            if (tag is CustomAttributeEncoding.Array)
            {
                arrayTag = parser.GetTag();
            }

            // Load the enum type if needed.
            if (tag is CustomAttributeEncoding.Enum
                || (tag is CustomAttributeEncoding.Array
                    && arrayTag is CustomAttributeEncoding.Enum))
            {
                // We cannot determine the underlying type without loading the enum.
                string enumTypeMaybe = parser.GetString() ?? throw new BadImageFormatException();
                enumType = TypeNameResolver.GetTypeReferencedByCustomAttribute(enumTypeMaybe, module);
                if (!enumType.IsEnum)
                {
                    throw new BadImageFormatException();
                }

                enumTag = RuntimeCustomAttributeData.TypeToCustomAttributeEncoding((RuntimeType)enumType.GetEnumUnderlyingType());
            }

            return new CustomAttributeType(tag, arrayTag, enumTag, enumType);
        }

        /// <summary>
        /// Used to parse CustomAttribute data. See ECMA-335 II.23.3.
        /// </summary>
        private ref struct CustomAttributeDataParser
        {
            private int _curr;
            private ReadOnlySpan<byte> _blob;

            public CustomAttributeDataParser(ConstArray attributeBlob)
            {
                unsafe
                {
                    _blob = new ReadOnlySpan<byte>((void*)attributeBlob.Signature, attributeBlob.Length);
                }
                _curr = 0;
            }

            private ReadOnlySpan<byte> PeekData(int size) => _blob.Slice(_curr, size);

            private ReadOnlySpan<byte> ReadData(int size)
            {
                ReadOnlySpan<byte> tmp = PeekData(size);
                Debug.Assert(size <= (_blob.Length - _curr));
                _curr += size;
                return tmp;
            }

            public byte GetU1()
            {
                ReadOnlySpan<byte> tmp = ReadData(sizeof(byte));
                return tmp[0];
            }

            public sbyte GetI1() => (sbyte)GetU1();

            public ushort GetU2()
            {
                ReadOnlySpan<byte> tmp = ReadData(sizeof(ushort));
                return BinaryPrimitives.ReadUInt16LittleEndian(tmp);
            }

            public short GetI2() => (short)GetU2();

            public uint GetU4()
            {
                ReadOnlySpan<byte> tmp = ReadData(sizeof(uint));
                return BinaryPrimitives.ReadUInt32LittleEndian(tmp);
            }

            public int GetI4() => (int)GetU4();

            public ulong GetU8()
            {
                ReadOnlySpan<byte> tmp = ReadData(sizeof(ulong));
                return BinaryPrimitives.ReadUInt64LittleEndian(tmp);
            }

            public long GetI8() => (long)GetU8();

            public float GetR4()
            {
                ReadOnlySpan<byte> tmp = ReadData(sizeof(float));
                return BinaryPrimitives.ReadSingleLittleEndian(tmp);
            }

            public CustomAttributeEncoding GetTag()
            {
                return (CustomAttributeEncoding)GetI1();
            }

            public double GetR8()
            {
                ReadOnlySpan<byte> tmp = ReadData(sizeof(double));
                return BinaryPrimitives.ReadDoubleLittleEndian(tmp);
            }

            public ushort GetProlog() => GetU2();

            public bool ValidateProlog()
            {
                ushort val = GetProlog();
                return val == 0x0001;
            }

            public string? GetString()
            {
                byte packedLengthBegin = PeekData(sizeof(byte))[0];

                // Check if the embedded string indicates a 'null' string (0xff).
                if (packedLengthBegin == 0xff) // ECMA 335- II.23.3
                {
                    // Consume the indicator.
                    ReadData(1);
                    return null;
                }

                // Not a null string, return a non-null string value.
                // The embedded string a UTF-8 prefixed by an ECMA-335 packed integer.
                int length = GetPackedLength(packedLengthBegin);
                if (length == 0)
                {
                    return string.Empty;
                }

                ReadOnlySpan<byte> utf8ByteSpan = ReadData(length);
                return Encoding.UTF8.GetString(utf8ByteSpan);
            }

            private int GetPackedLength(byte firstByte)
            {
                if ((firstByte & 0x80) == 0)
                {
                    // Consume one byte.
                    ReadData(1);
                    return firstByte & 0x7f;
                }

                int len;
                ReadOnlySpan<byte> data;
                if ((firstByte & 0xC0) == 0x80)
                {
                    // Consume the bytes.
                    data = ReadData(2);
                    len = (data[0] & 0x3f) << 8;
                    return len + data[1];
                }

                if ((firstByte & 0xE0) == 0xC0)
                {
                    // Consume the bytes.
                    data = ReadData(4);
                    len = (data[0] & 0x1f) << 24;
                    len += data[1] << 16;
                    len += data[2] << 8;
                    return len + data[3];
                }

                throw new OverflowException();
            }
        }
    }

    internal sealed class CustomAttributeCtorParameter(CustomAttributeType type)
    {
        public CustomAttributeType CustomAttributeType => type;
        public CustomAttributeEncodedArgument? EncodedArgument { get; set; }
    }

    internal sealed class CustomAttributeNamedParameter(MemberInfo memberInfo, CustomAttributeEncoding fieldOrProperty, CustomAttributeType type)
    {
        public MemberInfo MemberInfo => memberInfo;
        public CustomAttributeType CustomAttributeType => type;
        public CustomAttributeEncoding FieldOrProperty => fieldOrProperty;
        public CustomAttributeEncodedArgument? EncodedArgument { get; set; }
    }

    internal sealed class CustomAttributeType
    {
        public CustomAttributeType(
            CustomAttributeEncoding encodedType,
            CustomAttributeEncoding encodedArrayType,
            CustomAttributeEncoding encodedEnumType,
            Type? enumType)
        {
            EncodedType = encodedType;
            EncodedArrayType = encodedArrayType;
            EncodedEnumType = encodedEnumType;
            EnumType = enumType;
        }

        public CustomAttributeType(RuntimeType parameterType)
        {
            Debug.Assert(parameterType is not null);
            CustomAttributeEncoding encodedType = RuntimeCustomAttributeData.TypeToCustomAttributeEncoding(parameterType);
            CustomAttributeEncoding encodedArrayType = CustomAttributeEncoding.Undefined;
            CustomAttributeEncoding encodedEnumType = CustomAttributeEncoding.Undefined;
            Type? enumType = null;

            if (encodedType == CustomAttributeEncoding.Array)
            {
                parameterType = (RuntimeType)parameterType.GetElementType()!;
                encodedArrayType = RuntimeCustomAttributeData.TypeToCustomAttributeEncoding(parameterType);
            }

            if (encodedType == CustomAttributeEncoding.Enum
                || encodedArrayType == CustomAttributeEncoding.Enum)
            {
                enumType = parameterType;
                encodedEnumType = RuntimeCustomAttributeData.TypeToCustomAttributeEncoding((RuntimeType)Enum.GetUnderlyingType(parameterType));
            }

            EncodedType = encodedType;
            EncodedArrayType = encodedArrayType;
            EncodedEnumType = encodedEnumType;
            EnumType = enumType;
        }

        public CustomAttributeEncoding EncodedType { get; }
        public CustomAttributeEncoding EncodedEnumType { get; }
        public CustomAttributeEncoding EncodedArrayType { get; }

        /// The most complicated type is an enum[] in which case...
        public Type? EnumType { get; }
    }

    internal static unsafe partial class CustomAttribute
    {
        #region Internal Static Members
        internal static bool IsDefined(RuntimeType type, RuntimeType? caType, bool inherit)
        {
            Debug.Assert(type is not null);

            if (type.GetElementType() is not null)
                return false;

            if (PseudoCustomAttribute.IsDefined(type, caType))
                return true;

            if (IsCustomAttributeDefined(type.GetRuntimeModule(), type.MetadataToken, caType))
                return true;

            if (!inherit)
                return false;

            type = (type.BaseType as RuntimeType)!;

            while (type is not null)
            {
                if (IsCustomAttributeDefined(type.GetRuntimeModule(), type.MetadataToken, caType, 0, inherit))
                    return true;

                type = (type.BaseType as RuntimeType)!;
            }

            return false;
        }

        internal static bool IsDefined(RuntimeMethodInfo method, RuntimeType caType, bool inherit)
        {
            Debug.Assert(method is not null);
            Debug.Assert(caType is not null);

            if (PseudoCustomAttribute.IsDefined(method, caType))
                return true;

            if (IsCustomAttributeDefined(method.GetRuntimeModule(), method.MetadataToken, caType))
                return true;

            if (!inherit)
                return false;

            method = method.GetParentDefinition()!;

            while (method is not null)
            {
                if (IsCustomAttributeDefined(method.GetRuntimeModule(), method.MetadataToken, caType, 0, inherit))
                    return true;

                method = method.GetParentDefinition()!;
            }

            return false;
        }

        internal static bool IsDefined(RuntimeConstructorInfo ctor, RuntimeType caType)
        {
            Debug.Assert(ctor is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeConstructorInfo

            return IsCustomAttributeDefined(ctor.GetRuntimeModule(), ctor.MetadataToken, caType);
        }

        internal static bool IsDefined(RuntimePropertyInfo property, RuntimeType caType)
        {
            Debug.Assert(property is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimePropertyInfo

            return IsCustomAttributeDefined(property.GetRuntimeModule(), property.MetadataToken, caType);
        }

        internal static bool IsDefined(RuntimeEventInfo e, RuntimeType caType)
        {
            Debug.Assert(e is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeEventInfo

            return IsCustomAttributeDefined(e.GetRuntimeModule(), e.MetadataToken, caType);
        }

        internal static bool IsDefined(RuntimeFieldInfo field, RuntimeType caType)
        {
            Debug.Assert(field is not null);
            Debug.Assert(caType is not null);

            if (PseudoCustomAttribute.IsDefined(field, caType))
                return true;

            return IsCustomAttributeDefined(field.GetRuntimeModule(), field.MetadataToken, caType);
        }

        internal static bool IsDefined(RuntimeParameterInfo parameter, RuntimeType caType)
        {
            Debug.Assert(parameter is not null);
            Debug.Assert(caType is not null);

            if (PseudoCustomAttribute.IsDefined(parameter, caType))
                return true;

            return IsCustomAttributeDefined(parameter.GetRuntimeModule()!, parameter.MetadataToken, caType);
        }

        internal static bool IsDefined(RuntimeAssembly assembly, RuntimeType caType)
        {
            Debug.Assert(assembly is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeAssembly
            return IsCustomAttributeDefined((assembly.ManifestModule as RuntimeModule)!, RuntimeAssembly.GetToken(assembly), caType);
        }

        internal static bool IsDefined(RuntimeModule module, RuntimeType caType)
        {
            Debug.Assert(module is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeModule

            return IsCustomAttributeDefined(module, module.MetadataToken, caType);
        }

        internal static object[] GetCustomAttributes(RuntimeType type, RuntimeType caType, bool inherit)
        {
            Debug.Assert(type is not null);
            Debug.Assert(caType is not null);

            if (type.GetElementType() is not null)
                return CreateAttributeArrayHelper(caType, 0);

            if (type.IsGenericType && !type.IsGenericTypeDefinition)
                type = (type.GetGenericTypeDefinition() as RuntimeType)!;

            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(type, caType, ref pcas);

            // if we are asked to go up the hierarchy chain we have to do it now and regardless of the
            // attribute usage for the specific attribute because a derived attribute may override the usage...
            // ... however if the attribute is sealed we can rely on the attribute usage
            if (!inherit || (caType.IsSealed && !GetAttributeUsage(caType).Inherited))
            {
                object[] attributes = GetCustomAttributes(type.GetRuntimeModule(), type.MetadataToken, pcas.Count, caType);
                if (pcas.Count > 0) pcas.CopyTo(attributes, attributes.Length - pcas.Count);
                return attributes;
            }

            RuntimeType.ListBuilder<object> result = default;
            bool mustBeInheritable = false;

            for (int i = 0; i < pcas.Count; i++)
                result.Add(pcas[i]);

            do
            {
                AddCustomAttributes(ref result, type.GetRuntimeModule(), type.MetadataToken, caType, mustBeInheritable, result);
                mustBeInheritable = true;
                type = (type.BaseType as RuntimeType)!;
            } while (type != (RuntimeType)typeof(object) && type != null);

            object[] typedResult = CreateAttributeArrayHelper(caType, result.Count);
            for (int i = 0; i < result.Count; i++)
            {
                typedResult[i] = result[i];
            }
            return typedResult;
        }

        internal static object[] GetCustomAttributes(RuntimeMethodInfo method, RuntimeType caType, bool inherit)
        {
            Debug.Assert(method is not null);
            Debug.Assert(caType is not null);

            if (method.IsGenericMethod && !method.IsGenericMethodDefinition)
                method = (method.GetGenericMethodDefinition() as RuntimeMethodInfo)!;

            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(method, caType, ref pcas);

            // if we are asked to go up the hierarchy chain we have to do it now and regardless of the
            // attribute usage for the specific attribute because a derived attribute may override the usage...
            // ... however if the attribute is sealed we can rely on the attribute usage
            if (!inherit || (caType.IsSealed && !GetAttributeUsage(caType).Inherited))
            {
                object[] attributes = GetCustomAttributes(method.GetRuntimeModule(), method.MetadataToken, pcas.Count, caType);
                if (pcas.Count > 0) pcas.CopyTo(attributes, attributes.Length - pcas.Count);
                return attributes;
            }

            RuntimeType.ListBuilder<object> result = default;
            bool mustBeInheritable = false;

            for (int i = 0; i < pcas.Count; i++)
                result.Add(pcas[i]);

            while (method != null)
            {
                AddCustomAttributes(ref result, method.GetRuntimeModule(), method.MetadataToken, caType, mustBeInheritable, result);
                mustBeInheritable = true;
                method = method.GetParentDefinition()!;
            }

            object[] typedResult = CreateAttributeArrayHelper(caType, result.Count);
            for (int i = 0; i < result.Count; i++)
            {
                typedResult[i] = result[i];
            }
            return typedResult;
        }

        internal static object[] GetCustomAttributes(RuntimeConstructorInfo ctor, RuntimeType caType)
        {
            Debug.Assert(ctor != null);
            Debug.Assert(caType != null);

            // No pseudo attributes for RuntimeConstructorInfo

            return GetCustomAttributes(ctor.GetRuntimeModule(), ctor.MetadataToken, 0, caType);
        }

        internal static object[] GetCustomAttributes(RuntimePropertyInfo property, RuntimeType caType)
        {
            Debug.Assert(property is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimePropertyInfo

            return GetCustomAttributes(property.GetRuntimeModule(), property.MetadataToken, 0, caType);
        }

        internal static object[] GetCustomAttributes(RuntimeEventInfo e, RuntimeType caType)
        {
            Debug.Assert(e is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeEventInfo

            return GetCustomAttributes(e.GetRuntimeModule(), e.MetadataToken, 0, caType);
        }

        internal static object[] GetCustomAttributes(RuntimeFieldInfo field, RuntimeType caType)
        {
            Debug.Assert(field is not null);
            Debug.Assert(caType is not null);

            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(field, caType, ref pcas);
            object[] attributes = GetCustomAttributes(field.GetRuntimeModule(), field.MetadataToken, pcas.Count, caType);
            if (pcas.Count > 0) pcas.CopyTo(attributes, attributes.Length - pcas.Count);
            return attributes;
        }

        internal static object[] GetCustomAttributes(RuntimeParameterInfo parameter, RuntimeType caType)
        {
            Debug.Assert(parameter is not null);
            Debug.Assert(caType is not null);

            RuntimeType.ListBuilder<Attribute> pcas = default;
            PseudoCustomAttribute.GetCustomAttributes(parameter, caType, ref pcas);
            object[] attributes = GetCustomAttributes(parameter.GetRuntimeModule()!, parameter.MetadataToken, pcas.Count, caType);
            if (pcas.Count > 0) pcas.CopyTo(attributes, attributes.Length - pcas.Count);
            return attributes;
        }

        internal static object[] GetCustomAttributes(RuntimeAssembly assembly, RuntimeType caType)
        {
            Debug.Assert(assembly is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeAssembly

            int assemblyToken = RuntimeAssembly.GetToken(assembly);
            return GetCustomAttributes((assembly.ManifestModule as RuntimeModule)!, assemblyToken, 0, caType);
        }

        internal static object[] GetCustomAttributes(RuntimeModule module, RuntimeType caType)
        {
            Debug.Assert(module is not null);
            Debug.Assert(caType is not null);

            // No pseudo attributes for RuntimeModule

            return GetCustomAttributes(module, module.MetadataToken, 0, caType);
        }

        internal static bool IsAttributeDefined(RuntimeModule decoratedModule, int decoratedMetadataToken, int attributeCtorToken)
        {
            return IsCustomAttributeDefined(decoratedModule, decoratedMetadataToken, null, attributeCtorToken, false);
        }

        private static bool IsCustomAttributeDefined(
            RuntimeModule decoratedModule, int decoratedMetadataToken, RuntimeType? attributeFilterType)
        {
            return IsCustomAttributeDefined(decoratedModule, decoratedMetadataToken, attributeFilterType, 0, false);
        }

        private static bool IsCustomAttributeDefined(
            RuntimeModule decoratedModule, int decoratedMetadataToken, RuntimeType? attributeFilterType, int attributeCtorToken, bool mustBeInheritable)
        {
            MetadataImport scope = decoratedModule.MetadataImport;

            scope.EnumCustomAttributes(decoratedMetadataToken, out MetadataEnumResult attributeTokens);

            if (attributeTokens.Length == 0)
            {
                return false;
            }

            CustomAttributeRecord record = default;
            if (attributeFilterType is not null)
            {
                Debug.Assert(attributeCtorToken == 0);

                RuntimeType.ListBuilder<object> derivedAttributes = default;

                for (int i = 0; i < attributeTokens.Length; i++)
                {
                    scope.GetCustomAttributeProps(attributeTokens[i],
                        out record.tkCtor.Value, out record.blob);

                    if (FilterCustomAttributeRecord(record.tkCtor, in scope,
                        decoratedModule, decoratedMetadataToken, attributeFilterType, mustBeInheritable, ref derivedAttributes,
                        out _, out _, out _))
                    {
                        return true;
                    }
                }
            }
            else
            {
                Debug.Assert(attributeFilterType is null);
                Debug.Assert(!MetadataToken.IsNullToken(attributeCtorToken));

                for (int i = 0; i < attributeTokens.Length; i++)
                {
                    scope.GetCustomAttributeProps(attributeTokens[i],
                        out record.tkCtor.Value, out record.blob);

                    if (record.tkCtor == attributeCtorToken)
                    {
                        return true;
                    }
                }
            }
            GC.KeepAlive(decoratedModule);

            return false;
        }

        private static object[] GetCustomAttributes(
            RuntimeModule decoratedModule, int decoratedMetadataToken, int pcaCount, RuntimeType attributeFilterType)
        {
            RuntimeType.ListBuilder<object> attributes = default;

            AddCustomAttributes(ref attributes, decoratedModule, decoratedMetadataToken, attributeFilterType, false, default);

            object[] result = CreateAttributeArrayHelper(attributeFilterType, attributes.Count + pcaCount);
            for (int i = 0; i < attributes.Count; i++)
            {
                result[i] = attributes[i];
            }
            return result;
        }

        [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2070:MethodParameterDoesntMeetThisParameterRequirements",
            Justification = "Linker guarantees presence of all the constructor parameters, property setters and fields which are accessed by any " +
                            "attribute instantiation which is present in the code linker has analyzed." +
                            "As such the reflection usage in this method will never fail as those methods/fields will be present.")]
        private static void AddCustomAttributes(
            ref RuntimeType.ListBuilder<object> attributes,
            RuntimeModule decoratedModule, int decoratedMetadataToken,
            RuntimeType? attributeFilterType, bool mustBeInheritable,
            // The derivedAttributes list must be passed by value so that it is not modified with the discovered attributes
            RuntimeType.ListBuilder<object> derivedAttributes)
        {
            CustomAttributeRecord[] car = RuntimeCustomAttributeData.GetCustomAttributeRecords(decoratedModule, decoratedMetadataToken);

            if (attributeFilterType is null && car.Length == 0)
            {
                return;
            }

            MetadataImport scope = decoratedModule.MetadataImport;
            for (int i = 0; i < car.Length; i++)
            {
                ref CustomAttributeRecord caRecord = ref car[i];

                IntPtr blobStart = caRecord.blob.Signature;
                IntPtr blobEnd = (IntPtr)((byte*)blobStart + caRecord.blob.Length);

                if (!FilterCustomAttributeRecord(caRecord.tkCtor, in scope,
                                                 decoratedModule, decoratedMetadataToken, attributeFilterType!, mustBeInheritable,
                                                 ref derivedAttributes,
                                                 out RuntimeType attributeType, out IRuntimeMethodInfo? ctorWithParameters, out bool isVarArg))
                {
                    continue;
                }

                // Leverage RuntimeConstructorInfo standard .ctor verification
                RuntimeConstructorInfo.CheckCanCreateInstance(attributeType, isVarArg);

                // Create custom attribute object
                int cNamedArgs;
                object attribute;
                if (ctorWithParameters is not null)
                {
                    attribute = CreateCustomAttributeInstance(decoratedModule, attributeType, ctorWithParameters, ref blobStart, blobEnd, out cNamedArgs);
                }
                else
                {
                    attribute = attributeType.CreateInstanceDefaultCtor(publicOnly: false, wrapExceptions: false)!;

                    // It is allowed by the ECMA spec to have an empty signature blob
                    int blobLen = (int)((byte*)blobEnd - (byte*)blobStart);
                    if (blobLen == 0)
                    {
                        cNamedArgs = 0;
                    }
                    else
                    {
                        int data = Unsafe.ReadUnaligned<int>((void*)blobStart);
                        if (!BitConverter.IsLittleEndian)
                        {
                            // Metadata is always written in little-endian format. Must account for this on
                            // big-endian platforms.
                            data = BinaryPrimitives.ReverseEndianness(data);
                        }

                        const int CustomAttributeVersion = 0x0001;
                        if ((data & 0xffff) != CustomAttributeVersion)
                        {
                            throw new CustomAttributeFormatException();
                        }
                        cNamedArgs = data >> 16;

                        blobStart = (IntPtr)((byte*)blobStart + 4); // skip version and namedArgs count
                    }
                }

                for (int j = 0; j < cNamedArgs; j++)
                {
                    GetPropertyOrFieldData(decoratedModule, ref blobStart, blobEnd, out string name, out bool isProperty, out RuntimeType? type, out object? value);

                    try
                    {
                        if (isProperty)
                        {
                            if (type is null && value is not null)
                            {
                                type = (RuntimeType)value.GetType();
                                if (type == typeof(RuntimeType))
                                {
                                    type = (RuntimeType)typeof(Type);
                                }
                            }

                            RuntimePropertyInfo? property = (RuntimePropertyInfo?)(type is null ?
                                attributeType.GetProperty(name) :
                                attributeType.GetProperty(name, type, Type.EmptyTypes)) ??
                                throw new CustomAttributeFormatException(SR.Format(SR.RFLCT_InvalidPropFail, name));
                            RuntimeMethodInfo setMethod = property.GetSetMethod(true)!;

                            // Public properties may have non-public setter methods
                            if (!setMethod.IsPublic)
                            {
                                continue;
                            }

                            setMethod.InvokePropertySetter(attribute, BindingFlags.Default, null, value, null);
                        }
                        else
                        {
                            FieldInfo field = attributeType.GetField(name)!;
                            field.SetValue(attribute, value, BindingFlags.Default, Type.DefaultBinder, null);
                        }
                    }
                    catch (Exception e)
                    {
                        throw new CustomAttributeFormatException(
                            SR.Format(isProperty ? SR.RFLCT_InvalidPropFail : SR.RFLCT_InvalidFieldFail, name), e);
                    }
                }

                if (blobStart != blobEnd)
                {
                    throw new CustomAttributeFormatException();
                }

                attributes.Add(attribute);
            }
            GC.KeepAlive(decoratedModule);
        }

        [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2026:RequiresUnreferencedCode",
            Justification = "Module.ResolveMethod and Module.ResolveType are marked as RequiresUnreferencedCode because they rely on tokens" +
                            "which are not guaranteed to be stable across trimming. So if somebody hardcodes a token it could break." +
                            "The usage here is not like that as all these tokens come from existing metadata loaded from some IL" +
                            "and so trimming has no effect (the tokens are read AFTER trimming occurred).")]
        private static bool FilterCustomAttributeRecord(
            MetadataToken caCtorToken,
            in MetadataImport scope,
            RuntimeModule decoratedModule,
            MetadataToken decoratedToken,
            RuntimeType attributeFilterType,
            bool mustBeInheritable,
            ref RuntimeType.ListBuilder<object> derivedAttributes,
            out RuntimeType attributeType,
            out IRuntimeMethodInfo? ctorWithParameters,
            out bool isVarArg)
        {
            ctorWithParameters = null;
            isVarArg = false;

            // Resolve attribute type from ctor parent token found in decorated decoratedModule scope
            attributeType = (decoratedModule.ResolveType(scope.GetParentToken(caCtorToken), null, null) as RuntimeType)!;

            // Test attribute type against user provided attribute type filter
            if (!MatchesTypeFilter(attributeType, attributeFilterType))
                return false;

            // Ensure if attribute type must be inheritable that it is inheritable
            // Ensure that to consider a duplicate attribute type AllowMultiple is true
            if (!AttributeUsageCheck(attributeType, mustBeInheritable, ref derivedAttributes))
                return false;

            // Resolve the attribute ctor
            ConstArray ctorSig = scope.GetMethodSignature(caCtorToken);
            isVarArg = (ctorSig[0] & 0x05) != 0;
            bool ctorHasParameters = ctorSig[1] != 0;

            if (ctorHasParameters)
            {
                // Resolve method ctor token found in decorated decoratedModule scope
                // See https://github.com/dotnet/runtime/issues/11637 for why we fast-path non-generics here (fewer allocations)
                if (attributeType.IsGenericType)
                {
                    ctorWithParameters = decoratedModule.ResolveMethod(caCtorToken, attributeType.GenericTypeArguments, null)!.MethodHandle.GetMethodInfo();
                }
                else
                {
                    ctorWithParameters = new ModuleHandle(decoratedModule).ResolveMethodHandle(caCtorToken).GetMethodInfo();
                }
            }

            // Visibility checks
            MetadataToken tkParent = default;

            if (decoratedToken.IsParamDef)
            {
                tkParent = new MetadataToken(scope.GetParentToken(decoratedToken));
                tkParent = new MetadataToken(scope.GetParentToken(tkParent));
            }
            else if (decoratedToken.IsMethodDef || decoratedToken.IsProperty || decoratedToken.IsEvent || decoratedToken.IsFieldDef)
            {
                tkParent = new MetadataToken(scope.GetParentToken(decoratedToken));
            }
            else if (decoratedToken.IsTypeDef)
            {
                tkParent = decoratedToken;
            }
            else if (decoratedToken.IsGenericPar)
            {
                tkParent = new MetadataToken(scope.GetParentToken(decoratedToken));

                // decoratedToken is a generic parameter on a method. Get the declaring Type of the method.
                if (tkParent.IsMethodDef)
                    tkParent = new MetadataToken(scope.GetParentToken(tkParent));
            }
            else
            {
                // We need to relax this when we add support for other types of decorated tokens.
                Debug.Assert(decoratedToken.IsModule || decoratedToken.IsAssembly,
                                "The decoratedToken must be either an assembly, a module, a type, or a member.");
            }

            // If the attribute is on a type, member, or parameter we check access against the (declaring) type,
            // otherwise we check access against the module.
            RuntimeTypeHandle parentTypeHandle = tkParent.IsTypeDef ?
                                                    decoratedModule.ModuleHandle.ResolveTypeHandle(tkParent) :
                                                    default;

            RuntimeTypeHandle attributeTypeHandle = attributeType.TypeHandle;

            bool result = RuntimeMethodHandle.IsCAVisibleFromDecoratedType(new QCallTypeHandle(ref attributeTypeHandle),
                                                                    ctorWithParameters is not null ? ctorWithParameters.Value : RuntimeMethodHandleInternal.EmptyHandle,
                                                                    new QCallTypeHandle(ref parentTypeHandle),
                                                                    new QCallModule(ref decoratedModule)) != Interop.BOOL.FALSE;

            GC.KeepAlive(ctorWithParameters);
            return result;
        }

        private static bool MatchesTypeFilter(RuntimeType attributeType, RuntimeType attributeFilterType)
        {
            if (attributeFilterType.IsGenericTypeDefinition)
            {
                for (RuntimeType? type = attributeType; type != null; type = (RuntimeType?)type.BaseType)
                {
                    if (type.IsConstructedGenericType && type.GetGenericTypeDefinition() == attributeFilterType)
                    {
                        return true;
                    }
                }
                return false;
            }

            return attributeFilterType.IsAssignableFrom(attributeType);
        }
        #endregion

        #region Private Static Methods
        private static bool AttributeUsageCheck(
            RuntimeType attributeType, bool mustBeInheritable, ref RuntimeType.ListBuilder<object> derivedAttributes)
        {
            AttributeUsageAttribute? attributeUsageAttribute = null;

            if (mustBeInheritable)
            {
                attributeUsageAttribute = GetAttributeUsage(attributeType);

                if (!attributeUsageAttribute.Inherited)
                    return false;
            }

            // Legacy: AllowMultiple ignored for none inheritable attributes
            if (derivedAttributes.Count == 0)
                return true;

            for (int i = 0; i < derivedAttributes.Count; i++)
            {
                if (derivedAttributes[i].GetType() == attributeType)
                {
                    attributeUsageAttribute ??= GetAttributeUsage(attributeType);
                    return attributeUsageAttribute.AllowMultiple;
                }
            }

            return true;
        }

        [UnconditionalSuppressMessage("ReflectionAnalysis", "IL2026:RequiresUnreferencedCode",
            Justification = "Module.ResolveType is marked as RequiresUnreferencedCode because it relies on tokens" +
                            "which are not guaranteed to be stable across trimming. So if somebody hardcodes a token it could break." +
                            "The usage here is not like that as all these tokens come from existing metadata loaded from some IL" +
                            "and so trimming has no effect (the tokens are read AFTER trimming occurred).")]
        internal static AttributeUsageAttribute GetAttributeUsage(RuntimeType decoratedAttribute)
        {
            RuntimeModule decoratedModule = decoratedAttribute.GetRuntimeModule();
            MetadataImport scope = decoratedModule.MetadataImport;
            CustomAttributeRecord[] car = RuntimeCustomAttributeData.GetCustomAttributeRecords(decoratedModule, decoratedAttribute.MetadataToken);

            AttributeUsageAttribute? attributeUsageAttribute = null;

            for (int i = 0; i < car.Length; i++)
            {
                ref CustomAttributeRecord caRecord = ref car[i];
                RuntimeType? attributeType = decoratedModule.ResolveType(scope.GetParentToken(caRecord.tkCtor), null, null) as RuntimeType;

                if (attributeType != (RuntimeType)typeof(AttributeUsageAttribute))
                    continue;

                if (attributeUsageAttribute is not null)
                    throw new FormatException(SR.Format(SR.Format_AttributeUsage, attributeType));

                if (!ParseAttributeUsageAttribute(
                    caRecord.blob,
                    out AttributeTargets attrTargets,
                    out bool allowMultiple,
                    out bool inherited))
                {
                    throw new CustomAttributeFormatException();
                }

                attributeUsageAttribute = new AttributeUsageAttribute(attrTargets, allowMultiple: allowMultiple, inherited: inherited);
            }

            return attributeUsageAttribute ?? AttributeUsageAttribute.Default;
        }

        internal static object[] CreateAttributeArrayHelper(RuntimeType caType, int elementCount)
        {
            bool useAttributeArray = false;
            bool useObjectArray = false;

            if (caType == typeof(Attribute))
            {
                useAttributeArray = true;
            }
            else if (caType.IsActualValueType)
            {
                useObjectArray = true;
            }
            else if (caType.ContainsGenericParameters)
            {
                if (caType.IsSubclassOf(typeof(Attribute)))
                {
                    useAttributeArray = true;
                }
                else
                {
                    useObjectArray = true;
                }
            }

            if (useAttributeArray)
            {
                return elementCount == 0 ? Array.Empty<Attribute>() : new Attribute[elementCount];
            }
            if (useObjectArray)
            {
                return elementCount == 0 ? Array.Empty<object>() : new object[elementCount];
            }
            return elementCount == 0 ? caType.GetEmptyArray() : (object[])Array.CreateInstance(caType, elementCount);
        }
        #endregion

        [LibraryImport(RuntimeHelpers.QCall, EntryPoint = "CustomAttribute_ParseAttributeUsageAttribute")]
        [SuppressGCTransition]
        private static partial int ParseAttributeUsageAttribute(
            IntPtr pData,
            int cData,
            int* pTargets,
            int* pAllowMultiple,
            int* pInherited);

        private static bool ParseAttributeUsageAttribute(
            ConstArray blob,
            out AttributeTargets attrTargets,
            out bool allowMultiple,
            out bool inherited)
        {
            int attrTargetsLocal = 0;
            int allowMultipleLocal = 0;
            int inheritedLocal = 0;
            int result = ParseAttributeUsageAttribute(blob.Signature, blob.Length, &attrTargetsLocal, &allowMultipleLocal, &inheritedLocal);
            attrTargets = (AttributeTargets)attrTargetsLocal;
            allowMultiple = allowMultipleLocal != 0;
            inherited = inheritedLocal != 0;
            return result != 0;
        }

        [LibraryImport(RuntimeHelpers.QCall, EntryPoint = "CustomAttribute_CreateCustomAttributeInstance")]
        private static partial void CreateCustomAttributeInstance(
            QCallModule pModule,
            ObjectHandleOnStack type,
            ObjectHandleOnStack pCtor,
            ref IntPtr ppBlob,
            IntPtr pEndBlob,
            out int pcNamedArgs,
            ObjectHandleOnStack instance);

        private static object CreateCustomAttributeInstance(RuntimeModule module, RuntimeType type, IRuntimeMethodInfo ctor, ref IntPtr blob, IntPtr blobEnd, out int namedArgs)
        {
            if (module is null)
            {
                throw new ArgumentNullException(null, SR.Arg_InvalidHandle);
            }

            object? result = null;
            CreateCustomAttributeInstance(
                new QCallModule(ref module),
                ObjectHandleOnStack.Create(ref type),
                ObjectHandleOnStack.Create(ref ctor),
                ref blob,
                blobEnd,
                out namedArgs,
                ObjectHandleOnStack.Create(ref result));
            return result!;
        }

        [LibraryImport(RuntimeHelpers.QCall, EntryPoint = "CustomAttribute_CreatePropertyOrFieldData", StringMarshalling = StringMarshalling.Utf16)]
        private static partial void CreatePropertyOrFieldData(
            QCallModule pModule,
            ref IntPtr ppBlobStart,
            IntPtr pBlobEnd,
            StringHandleOnStack name,
            [MarshalAs(UnmanagedType.Bool)] out bool bIsProperty,
            ObjectHandleOnStack type,
            ObjectHandleOnStack value);

        private static void GetPropertyOrFieldData(
            RuntimeModule module, ref IntPtr blobStart, IntPtr blobEnd, out string name, out bool isProperty, out RuntimeType? type, out object? value)
        {
            if (module is null)
            {
                throw new ArgumentNullException(null, SR.Arg_InvalidHandle);
            }

            string? nameLocal = null;
            RuntimeType? typeLocal = null;
            object? valueLocal = null;
            CreatePropertyOrFieldData(
                new QCallModule(ref module),
                ref blobStart,
                blobEnd,
                new StringHandleOnStack(ref nameLocal),
                out isProperty,
                ObjectHandleOnStack.Create(ref typeLocal),
                ObjectHandleOnStack.Create(ref valueLocal));
            name = nameLocal!;
            type = typeLocal;
            value = valueLocal;
        }
    }

    internal static class PseudoCustomAttribute
    {
        #region Private Static Data Members
        // Here we can avoid the need to take a lock when using Dictionary by rearranging
        // the only method that adds values to the Dictionary. For more details on
        // Dictionary versus Hashtable thread safety:
        // See code:Dictionary#DictionaryVersusHashtableThreadSafety
        private static readonly HashSet<RuntimeType> s_pca = CreatePseudoCustomAttributeHashSet();
        #endregion

        #region Static Constructor
        private static HashSet<RuntimeType> CreatePseudoCustomAttributeHashSet()
        {
            Type[] pcas =
            [
                // See https://github.com/dotnet/runtime/blob/main/src/coreclr/md/compiler/custattr_emit.cpp
                typeof(FieldOffsetAttribute), // field
                typeof(SerializableAttribute), // class, struct, enum, delegate
                typeof(MarshalAsAttribute), // parameter, field, return-value
                typeof(ComImportAttribute), // class, interface
                typeof(NonSerializedAttribute), // field, inherited
                typeof(InAttribute), // parameter
                typeof(OutAttribute), // parameter
                typeof(OptionalAttribute), // parameter
                typeof(DllImportAttribute), // method
                typeof(PreserveSigAttribute), // method
                typeof(TypeForwardedToAttribute), // assembly
            ];

            HashSet<RuntimeType> set = new HashSet<RuntimeType>(pcas.Length);
            foreach (RuntimeType runtimeType in pcas)
            {
                VerifyPseudoCustomAttribute(runtimeType);
                set.Add(runtimeType);
            }
            return set;
        }

        [Conditional("DEBUG")]
        private static void VerifyPseudoCustomAttribute(RuntimeType pca)
        {
            // If any of these are invariants are no longer true will have to
            // re-architect the PCA product logic and test cases.
            Debug.Assert(pca.BaseType == typeof(Attribute), "Pseudo CA Error - Incorrect base type");
            AttributeUsageAttribute usage = CustomAttribute.GetAttributeUsage(pca);
            Debug.Assert(!usage.Inherited, "Pseudo CA Error - Unexpected Inherited value");
            if (pca == typeof(TypeForwardedToAttribute))
            {
                Debug.Assert(usage.AllowMultiple, "Pseudo CA Error - Unexpected AllowMultiple value");
            }
            else
            {
                Debug.Assert(!usage.AllowMultiple, "Pseudo CA Error - Unexpected AllowMultiple value");
            }
        }
        #endregion

        #region Internal Static
        internal static void GetCustomAttributes(RuntimeType type, RuntimeType caType, ref RuntimeType.ListBuilder<Attribute> pcas)
        {
            Debug.Assert(type is not null);
            Debug.Assert(caType is not null);

            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType))
                return;

#pragma warning disable SYSLIB0050 // Legacy serialization infrastructure is obsolete
            if (all || caType == typeof(SerializableAttribute))
            {
                if ((type.Attributes & TypeAttributes.Serializable) != 0)
                    pcas.Add(new SerializableAttribute());
            }
#pragma warning restore SYSLIB0050
            if (all || caType == typeof(ComImportAttribute))
            {
                if ((type.Attributes & TypeAttributes.Import) != 0)
                    pcas.Add(new ComImportAttribute());
            }
        }
        internal static bool IsDefined(RuntimeType type, RuntimeType? caType)
        {
            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType!))
                return false;

#pragma warning disable SYSLIB0050 // Legacy serialization infrastructure is obsolete
            if (all || caType == typeof(SerializableAttribute))
            {
                if ((type.Attributes & TypeAttributes.Serializable) != 0)
                    return true;
            }
#pragma warning restore SYSLIB0050
            if (all || caType == typeof(ComImportAttribute))
            {
                if ((type.Attributes & TypeAttributes.Import) != 0)
                    return true;
            }

            return false;
        }

        internal static void GetCustomAttributes(RuntimeMethodInfo method, RuntimeType caType, ref RuntimeType.ListBuilder<Attribute> pcas)
        {
            Debug.Assert(method is not null);
            Debug.Assert(caType is not null);

            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType))
                return;

            if (all || caType == typeof(DllImportAttribute))
            {
                Attribute? pca = GetDllImportCustomAttribute(method);
                if (pca is not null) pcas.Add(pca);
            }
            if (all || caType == typeof(PreserveSigAttribute))
            {
                if ((method.GetMethodImplementationFlags() & MethodImplAttributes.PreserveSig) != 0)
                    pcas.Add(new PreserveSigAttribute());
            }
        }
        internal static bool IsDefined(RuntimeMethodInfo method, RuntimeType? caType)
        {
            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType!))
                return false;

            if (all || caType == typeof(DllImportAttribute))
            {
                if ((method.Attributes & MethodAttributes.PinvokeImpl) != 0)
                    return true;
            }
            if (all || caType == typeof(PreserveSigAttribute))
            {
                if ((method.GetMethodImplementationFlags() & MethodImplAttributes.PreserveSig) != 0)
                    return true;
            }

            return false;
        }

        internal static void GetCustomAttributes(RuntimeParameterInfo parameter, RuntimeType caType, ref RuntimeType.ListBuilder<Attribute> pcas)
        {
            Debug.Assert(parameter is not null);
            Debug.Assert(caType is not null);

            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType))
                return;

            if (all || caType == typeof(InAttribute))
            {
                if (parameter.IsIn)
                    pcas.Add(new InAttribute());
            }
            if (all || caType == typeof(OutAttribute))
            {
                if (parameter.IsOut)
                    pcas.Add(new OutAttribute());
            }
            if (all || caType == typeof(OptionalAttribute))
            {
                if (parameter.IsOptional)
                    pcas.Add(new OptionalAttribute());
            }
            if (all || caType == typeof(MarshalAsAttribute))
            {
                Attribute? pca = GetMarshalAsCustomAttribute(parameter);
                if (pca is not null) pcas.Add(pca);
            }
        }
        internal static bool IsDefined(RuntimeParameterInfo parameter, RuntimeType? caType)
        {
            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType!))
                return false;

            if (all || caType == typeof(InAttribute))
            {
                if (parameter.IsIn) return true;
            }
            if (all || caType == typeof(OutAttribute))
            {
                if (parameter.IsOut) return true;
            }
            if (all || caType == typeof(OptionalAttribute))
            {
                if (parameter.IsOptional) return true;
            }
            if (all || caType == typeof(MarshalAsAttribute))
            {
                if (GetMarshalAsCustomAttribute(parameter) is not null) return true;
            }

            return false;
        }

        internal static void GetCustomAttributes(RuntimeFieldInfo field, RuntimeType caType, ref RuntimeType.ListBuilder<Attribute> pcas)
        {
            Debug.Assert(field is not null);
            Debug.Assert(caType is not null);

            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType))
                return;

            Attribute? pca;

            if (all || caType == typeof(MarshalAsAttribute))
            {
                pca = GetMarshalAsCustomAttribute(field);
                if (pca is not null) pcas.Add(pca);
            }
            if (all || caType == typeof(FieldOffsetAttribute))
            {
                pca = GetFieldOffsetCustomAttribute(field);
                if (pca is not null) pcas.Add(pca);
            }
#pragma warning disable SYSLIB0050 // Legacy serialization infrastructure is obsolete
            if (all || caType == typeof(NonSerializedAttribute))
            {
                if ((field.Attributes & FieldAttributes.NotSerialized) != 0)
                    pcas.Add(new NonSerializedAttribute());
            }
#pragma warning restore SYSLIB0050
        }
        internal static bool IsDefined(RuntimeFieldInfo field, RuntimeType? caType)
        {
            bool all = caType == typeof(object) || caType == typeof(Attribute);
            if (!all && !s_pca.Contains(caType!))
                return false;

            if (all || caType == typeof(MarshalAsAttribute))
            {
                if (GetMarshalAsCustomAttribute(field) is not null) return true;
            }
            if (all || caType == typeof(FieldOffsetAttribute))
            {
                if (GetFieldOffsetCustomAttribute(field) is not null) return true;
            }
#pragma warning disable SYSLIB0050 // Legacy serialization infrastructure is obsolete
            if (all || caType == typeof(NonSerializedAttribute))
            {
                if ((field.Attributes & FieldAttributes.NotSerialized) != 0)
                    return true;
            }
#pragma warning restore SYSLIB0050

            return false;
        }
        #endregion

        private static DllImportAttribute? GetDllImportCustomAttribute(RuntimeMethodInfo method)
        {
            if ((method.Attributes & MethodAttributes.PinvokeImpl) == 0)
                return null;

            RuntimeModule module = method.Module.ModuleHandle.GetRuntimeModule();
            MetadataImport scope = module.MetadataImport;
            int token = method.MetadataToken;
            scope.GetPInvokeMap(token, out PInvokeAttributes flags, out string entryPoint, out string dllName);
            GC.KeepAlive(module);

            CharSet charSet = CharSet.None;

            switch (flags & PInvokeAttributes.CharSetMask)
            {
                case PInvokeAttributes.CharSetNotSpec: charSet = CharSet.None; break;
                case PInvokeAttributes.CharSetAnsi: charSet = CharSet.Ansi; break;
                case PInvokeAttributes.CharSetUnicode: charSet = CharSet.Unicode; break;
                case PInvokeAttributes.CharSetAuto: charSet = CharSet.Auto; break;

                // Invalid: default to CharSet.None
                default: break;
            }

            CallingConvention callingConvention = CallingConvention.Cdecl;

            switch (flags & PInvokeAttributes.CallConvMask)
            {
                case PInvokeAttributes.CallConvWinapi: callingConvention = CallingConvention.Winapi; break;
                case PInvokeAttributes.CallConvCdecl: callingConvention = CallingConvention.Cdecl; break;
                case PInvokeAttributes.CallConvStdcall: callingConvention = CallingConvention.StdCall; break;
                case PInvokeAttributes.CallConvThiscall: callingConvention = CallingConvention.ThisCall; break;
                case PInvokeAttributes.CallConvFastcall: callingConvention = CallingConvention.FastCall; break;

                // Invalid: default to CallingConvention.Cdecl
                default: break;
            }

            DllImportAttribute attribute = new DllImportAttribute(dllName);

            attribute.EntryPoint = entryPoint;
            attribute.CharSet = charSet;
            attribute.SetLastError = (flags & PInvokeAttributes.SupportsLastError) != 0;
            attribute.ExactSpelling = (flags & PInvokeAttributes.NoMangle) != 0;
            attribute.PreserveSig = (method.GetMethodImplementationFlags() & MethodImplAttributes.PreserveSig) != 0;
            attribute.CallingConvention = callingConvention;
            attribute.BestFitMapping = (flags & PInvokeAttributes.BestFitMask) == PInvokeAttributes.BestFitEnabled;
            attribute.ThrowOnUnmappableChar = (flags & PInvokeAttributes.ThrowOnUnmappableCharMask) == PInvokeAttributes.ThrowOnUnmappableCharEnabled;

            return attribute;
        }

        private static MarshalAsAttribute? GetMarshalAsCustomAttribute(RuntimeParameterInfo parameter)
        {
            return GetMarshalAsCustomAttribute(parameter.MetadataToken, parameter.GetRuntimeModule()!);
        }

        private static MarshalAsAttribute? GetMarshalAsCustomAttribute(RuntimeFieldInfo field)
        {
            return GetMarshalAsCustomAttribute(field.MetadataToken, field.GetRuntimeModule());
        }

        private static MarshalAsAttribute? GetMarshalAsCustomAttribute(int token, RuntimeModule scope)
        {
            ConstArray nativeType = scope.MetadataImport.GetFieldMarshal(token);

            if (nativeType.Length == 0)
                return null;

            return MetadataImport.GetMarshalAs(nativeType, scope);
        }

        private static FieldOffsetAttribute? GetFieldOffsetCustomAttribute(RuntimeFieldInfo field)
        {
            if (field.DeclaringType is not null)
            {
                RuntimeModule module = field.GetRuntimeModule();
                if (module.MetadataImport.GetFieldOffset(field.DeclaringType.MetadataToken, field.MetadataToken, out int fieldOffset))
                {
                    return new FieldOffsetAttribute(fieldOffset);
                }
                GC.KeepAlive(module);
            }
            return null;
        }

        internal static StructLayoutAttribute? GetStructLayoutCustomAttribute(RuntimeType type)
        {
            if (type.IsInterface || type.HasElementType || type.IsGenericParameter)
                return null;

            LayoutKind layoutKind = LayoutKind.Auto;
            switch (type.Attributes & TypeAttributes.LayoutMask)
            {
                case TypeAttributes.ExplicitLayout: layoutKind = LayoutKind.Explicit; break;
                case TypeAttributes.AutoLayout: layoutKind = LayoutKind.Auto; break;
                case TypeAttributes.SequentialLayout: layoutKind = LayoutKind.Sequential; break;
                default: Debug.Fail("Unreachable code"); break;
            }

            CharSet charSet = CharSet.None;
            switch (type.Attributes & TypeAttributes.StringFormatMask)
            {
                case TypeAttributes.AnsiClass: charSet = CharSet.Ansi; break;
                case TypeAttributes.AutoClass: charSet = CharSet.Auto; break;
                case TypeAttributes.UnicodeClass: charSet = CharSet.Unicode; break;
                default: Debug.Fail("Unreachable code"); break;
            }
            RuntimeModule module = type.GetRuntimeModule();
            module.MetadataImport.GetClassLayout(type.MetadataToken, out int pack, out int size);
            GC.KeepAlive(module);

            StructLayoutAttribute attribute = new StructLayoutAttribute(layoutKind);

            attribute.Pack = pack;
            attribute.Size = size;
            attribute.CharSet = charSet;

            return attribute;
        }
    }
}
