﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Generic;
using System.Threading.Tasks;
using Xunit;

namespace System.Text.Json.Serialization.Tests
{
    public class InvalidTypeTests_Span : InvalidTypeTests
    {
        public InvalidTypeTests_Span() : base(JsonSerializerWrapper.SpanSerializer) { }
    }

    public class InvalidTypeTests_String : InvalidTypeTests
    {
        public InvalidTypeTests_String() : base(JsonSerializerWrapper.StringSerializer) { }
    }

    public class InvalidTypeTests_AsyncStream : InvalidTypeTests
    {
        public InvalidTypeTests_AsyncStream() : base(JsonSerializerWrapper.AsyncStreamSerializer) { }
    }

    public class InvalidTypeTests_AsyncStreamWithSmallBuffer : InvalidTypeTests
    {
        public InvalidTypeTests_AsyncStreamWithSmallBuffer() : base(JsonSerializerWrapper.AsyncStreamSerializerWithSmallBuffer) { }
    }

    public class InvalidTypeTests_SyncStream : InvalidTypeTests
    {
        public InvalidTypeTests_SyncStream() : base(JsonSerializerWrapper.SyncStreamSerializer) { }
    }

    public class InvalidTypeTests_Writer : InvalidTypeTests
    {
        public InvalidTypeTests_Writer() : base(JsonSerializerWrapper.ReaderWriterSerializer) { }
    }

    public class InvalidTypeTests_Pipe : InvalidTypeTests
    {
        public InvalidTypeTests_Pipe() : base(JsonSerializerWrapper.AsyncPipeSerializer) { }
    }

    public class InvalidTypeTests_PipeWithSmallBuffer : InvalidTypeTests
    {
        public InvalidTypeTests_PipeWithSmallBuffer() : base(JsonSerializerWrapper.AsyncPipeSerializerWithSmallBuffer) { }
    }

    public abstract class InvalidTypeTests
    {
        private JsonSerializerWrapper Serializer { get; }

        public InvalidTypeTests(JsonSerializerWrapper serializer)
        {
            Serializer = serializer;
        }

        [Theory]
        [MemberData(nameof(OpenGenericTypes))]
        [MemberData(nameof(RefStructTypes))]
        [MemberData(nameof(PointerTypes))]
        public void DeserializeInvalidType(Type type)
        {
            InvalidOperationException ex = Assert.Throws<InvalidOperationException>(() => JsonSerializer.Deserialize("", type));
            Assert.Contains(type.ToString(), ex.Message);
        }

        [Theory]
        [MemberData(nameof(TypesWithInvalidMembers_WithMembers))]
        public async Task TypeWithInvalidMember(Type classType, Type invalidMemberType, string invalidMemberName)
        {
            static void ValidateException(InvalidOperationException ex, Type classType, Type invalidMemberType, string invalidMemberName)
            {
                string exAsStr = ex.ToString();
                Assert.Contains(invalidMemberType.ToString(), exAsStr);
                Assert.Contains(invalidMemberName, exAsStr);
                Assert.Contains(classType.ToString(), exAsStr);
            }

            object obj = Activator.CreateInstance(classType);
            InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() => Serializer.SerializeWrapper(obj, classType));
            ValidateException(ex, classType, invalidMemberType, invalidMemberName);

            ex = await Assert.ThrowsAsync<InvalidOperationException>(() => Serializer.SerializeWrapper(null, classType));
            ValidateException(ex, classType, invalidMemberType, invalidMemberName);

            ex = Assert.Throws<InvalidOperationException>(() => JsonSerializer.Deserialize("", classType));
            ValidateException(ex, classType, invalidMemberType, invalidMemberName);
        }

        [Theory]
        [MemberData(nameof(OpenGenericTypes_ToSerialize))]
        public async Task SerializeOpenGeneric(Type type)
        {
            object obj;

            if (type.GetGenericArguments().Length == 1)
            {
                obj = Activator.CreateInstance(type.MakeGenericType(typeof(int)));
            }
            else
            {
                obj = Activator.CreateInstance(type.MakeGenericType(typeof(string), typeof(int)));
            }

            await Assert.ThrowsAsync<ArgumentException>(() => Serializer.SerializeWrapper(obj, type));
        }

        [Theory]
        [MemberData(nameof(OpenGenericTypes))]
        public async Task SerializeInvalidTypes_NullValue(Type type)
        {
            InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() => Serializer.SerializeWrapper(null, type));
            Assert.Contains(type.ToString(), ex.Message);
        }

        [Fact]
        public async Task SerializeOpenGeneric_NullableOfT()
        {
            Type openNullableType = typeof(Nullable<>);
            object obj = Activator.CreateInstance(openNullableType.MakeGenericType(typeof(int)));

            InvalidOperationException ex = await Assert.ThrowsAsync<InvalidOperationException>(() => Serializer.SerializeWrapper(obj, openNullableType));
            Assert.Contains(openNullableType.ToString(), ex.Message);
        }

        private class Test<T> { }

        public static IEnumerable<object[]> OpenGenericTypes()
        {
            yield return new object[] { typeof(Test<>) };
            yield return new object[] { typeof(Nullable<>) };
            yield return new object[] { typeof(IList<>) };
            yield return new object[] { typeof(List<>) };
            yield return new object[] { typeof(List<>).MakeGenericType(typeof(Test<>)) };
            yield return new object[] { typeof(Test<>).MakeGenericType(typeof(List<>)) };
            yield return new object[] { typeof(Dictionary<,>) };
            yield return new object[] { typeof(Dictionary<,>).MakeGenericType(typeof(string), typeof(Nullable<>)) };
            yield return new object[] { typeof(Dictionary<,>).MakeGenericType(typeof(Nullable<>), typeof(string)) };
        }

        public static IEnumerable<object[]> OpenGenericTypes_ToSerialize()
        {
            yield return new object[] { typeof(Test<>) };
            yield return new object[] { typeof(List<>) };
            yield return new object[] { typeof(Dictionary<,>) };
        }

        public static IEnumerable<object[]> RefStructTypes()
        {
            yield return new object[] { typeof(Span<int>) };
            yield return new object[] { typeof(Utf8JsonReader) };
            yield return new object[] { typeof(MyRefStruct) };
        }

        private static readonly Type s_intPtrType = typeof(int*); // Unsafe code may not appear in iterators.

        public static IEnumerable<object[]> PointerTypes()
        {
            yield return new object[] { s_intPtrType };
        }

        // Instances of the types of the invalid members cannot be passed directly
        // to the serializer on serialization due to type constraints,
        // e.g. int* can't be boxed and passed to the non-generic overload,
        // and typeof(int*) can't be a generic parameter to the generic overload.
        public static IEnumerable<object[]> TypesWithInvalidMembers_WithMembers()
        {
            yield return new object[] { typeof(ClassWithSpan), typeof(Span<byte>), "Span" };

            yield return new object[] { typeof(ClassWithIntPtr), s_intPtrType, "IntPtr" };
        }

        private class ClassWithSpan
        {
            public Span<byte> Span => Array.Empty<byte>();
        }

        private class ClassWithIntPtr
        {
            public unsafe int* IntPtr { get; }
        }

        private ref struct MyRefStruct { }

        [Fact]
        public void ArraySegmentTest()
        {
            var obj = new ClassWithArraySegment()
            {
                ArraySegment = new ArraySegment<byte>(new byte[] { 1 }),
            };

            string serialized = JsonSerializer.Serialize(obj);
            Assert.Equal(@"{""ArraySegment"":[1]}", serialized);

            NotSupportedException ex = Assert.Throws<NotSupportedException>(() => JsonSerializer.Deserialize<ClassWithArraySegment>(serialized));
            Assert.Contains(typeof(ArraySegment<byte>).ToString(), ex.Message);
        }

        private class ClassWithArraySegment
        {
            public ArraySegment<byte> ArraySegment { get; set; }
        }
    }
}
