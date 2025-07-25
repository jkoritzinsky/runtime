﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Threading.Tasks;
using Microsoft.DotNet.RemoteExecutor;
using Newtonsoft.Json;
using Xunit;

namespace System.Text.Json.Serialization.Tests
{
    public class EnumConverterTests
    {
        private static readonly JsonSerializerOptions s_optionsWithStringEnumConverter = new() { Converters = { new JsonStringEnumConverter() } };
        private static readonly JsonSerializerOptions s_optionsWithStringAndNoIntegerEnumConverter = new() { Converters = { new JsonStringEnumConverter(allowIntegerValues: false) } };

        [Theory]
        [InlineData(typeof(JsonStringEnumConverter), typeof(DayOfWeek))]
        [InlineData(typeof(JsonStringEnumConverter), typeof(MyCustomEnum))]
        [InlineData(typeof(JsonStringEnumConverter<DayOfWeek>), typeof(DayOfWeek))]
        [InlineData(typeof(JsonStringEnumConverter<MyCustomEnum>), typeof(MyCustomEnum))]
        public static void JsonStringEnumConverter_SupportedType_WorksAsExpected(Type converterType, Type supportedType)
        {
            var options = new JsonSerializerOptions();
            var factory = (JsonConverterFactory)Activator.CreateInstance(converterType);

            Assert.True(factory.CanConvert(supportedType));

            JsonConverter converter = factory.CreateConverter(supportedType, options);
            Assert.Equal(supportedType, converter.Type);
        }

        [Theory]
        [InlineData(typeof(JsonStringEnumConverter), typeof(int))]
        [InlineData(typeof(JsonStringEnumConverter), typeof(string))]
        [InlineData(typeof(JsonStringEnumConverter), typeof(JsonStringEnumConverter))]
        [InlineData(typeof(JsonStringEnumConverter<DayOfWeek>), typeof(int))]
        [InlineData(typeof(JsonStringEnumConverter<DayOfWeek>), typeof(string))]
        [InlineData(typeof(JsonStringEnumConverter<DayOfWeek>), typeof(JsonStringEnumConverter<MyCustomEnum>))]
        [InlineData(typeof(JsonStringEnumConverter<DayOfWeek>), typeof(MyCustomEnum))]
        [InlineData(typeof(JsonStringEnumConverter<MyCustomEnum>), typeof(DayOfWeek))]
        public static void JsonStringEnumConverter_InvalidType_ThrowsArgumentOutOfRangeException(Type converterType, Type unsupportedType)
        {
            var options = new JsonSerializerOptions();
            var factory = (JsonConverterFactory)Activator.CreateInstance(converterType);

            Assert.False(factory.CanConvert(unsupportedType));
            ArgumentOutOfRangeException ex = Assert.Throws<ArgumentOutOfRangeException>(() => factory.CreateConverter(unsupportedType, options));
            Assert.Contains(unsupportedType.FullName, ex.Message);
        }

        [Theory]
        [InlineData(typeof(JsonNumberEnumConverter<DayOfWeek>), typeof(DayOfWeek))]
        [InlineData(typeof(JsonNumberEnumConverter<MyCustomEnum>), typeof(MyCustomEnum))]
        [DynamicDependency(DynamicallyAccessedMemberTypes.PublicConstructors, typeof(JsonNumberEnumConverter<>))]
        public static void JsonNumberEnumConverter_SupportedType_WorksAsExpected(Type converterType, Type supportedType)
        {
            var options = new JsonSerializerOptions();
            var factory = (JsonConverterFactory)Activator.CreateInstance(converterType);

            Assert.True(factory.CanConvert(supportedType));

            JsonConverter converter = factory.CreateConverter(supportedType, options);
            Assert.Equal(supportedType, converter.Type);
        }

        [Theory]
        [InlineData(typeof(JsonNumberEnumConverter<DayOfWeek>), typeof(int))]
        [InlineData(typeof(JsonNumberEnumConverter<DayOfWeek>), typeof(string))]
        [InlineData(typeof(JsonNumberEnumConverter<DayOfWeek>), typeof(JsonStringEnumConverter<MyCustomEnum>))]
        [InlineData(typeof(JsonNumberEnumConverter<DayOfWeek>), typeof(MyCustomEnum))]
        [InlineData(typeof(JsonNumberEnumConverter<MyCustomEnum>), typeof(DayOfWeek))]
        [DynamicDependency(DynamicallyAccessedMemberTypes.PublicConstructors, typeof(JsonNumberEnumConverter<>))]
        public static void JsonNumberEnumConverter_InvalidType_ThrowsArgumentOutOfRangeException(Type converterType, Type unsupportedType)
        {
            var options = new JsonSerializerOptions();
            var factory = (JsonConverterFactory)Activator.CreateInstance(converterType);

            Assert.False(factory.CanConvert(unsupportedType));
            ArgumentOutOfRangeException ex = Assert.Throws<ArgumentOutOfRangeException>(() => factory.CreateConverter(unsupportedType, options));
            Assert.Contains(unsupportedType.FullName, ex.Message);
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public void ConvertDayOfWeek(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<DayOfWeek>(useGenericVariant);

            WhenClass when = JsonSerializer.Deserialize<WhenClass>(@"{""Day"":""Monday""}", options);
            Assert.Equal(DayOfWeek.Monday, when.Day);
            DayOfWeek day = JsonSerializer.Deserialize<DayOfWeek>(@"""Tuesday""", options);
            Assert.Equal(DayOfWeek.Tuesday, day);

            // We are case insensitive on read
            day = JsonSerializer.Deserialize<DayOfWeek>(@"""wednesday""", options);
            Assert.Equal(DayOfWeek.Wednesday, day);

            // Numbers work by default
            day = JsonSerializer.Deserialize<DayOfWeek>(@"4", options);
            Assert.Equal(DayOfWeek.Thursday, day);

            string json = JsonSerializer.Serialize(DayOfWeek.Friday, options);
            Assert.Equal(@"""Friday""", json);

            // Try a unique naming policy
            options = CreateStringEnumOptionsForType<DayOfWeek>(useGenericVariant, new ToLowerNamingPolicy());

            json = JsonSerializer.Serialize(DayOfWeek.Friday, options);
            Assert.Equal(@"""friday""", json);

            // Undefined values should come out as a number (not a string)
            json = JsonSerializer.Serialize((DayOfWeek)(-1), options);
            Assert.Equal(@"-1", json);

            // Not permitting integers should throw
            options = CreateStringEnumOptionsForType<DayOfWeek>(useGenericVariant, allowIntegerValues: false);
            Assert.Throws<JsonException>(() => JsonSerializer.Serialize((DayOfWeek)(-1), options));

            // Quoted numbers should throw
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>("1", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>("-1", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@"""1""", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@"""+1""", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@"""-1""", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@""" 1 """, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@""" +1 """, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@""" -1 """, options));

            day = JsonSerializer.Deserialize<DayOfWeek>(@"""Monday""", options);
            Assert.Equal(DayOfWeek.Monday, day);

            // Numbers-formatted json string should first consider naming policy
            options = CreateStringEnumOptionsForType<DayOfWeek>(useGenericVariant, new ToEnumNumberNamingPolicy<DayOfWeek>(), false);
            day = JsonSerializer.Deserialize<DayOfWeek>(@"""1""", options);
            Assert.Equal(DayOfWeek.Monday, day);

            options = CreateStringEnumOptionsForType<DayOfWeek>(useGenericVariant, new ToLowerNamingPolicy(), false);
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<DayOfWeek>(@"""1""", options));
        }

        public class ToLowerNamingPolicy : JsonNamingPolicy
        {
            public override string ConvertName(string name) => name.ToLowerInvariant();
        }

        public class WhenClass
        {
            public DayOfWeek Day { get; set; }
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public void ConvertFileAttributes(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<FileAttributes>(useGenericVariant);

            FileState state = JsonSerializer.Deserialize<FileState>(@"{""Attributes"":""ReadOnly""}", options);
            Assert.Equal(FileAttributes.ReadOnly, state.Attributes);
            state = JsonSerializer.Deserialize<FileState>(@"{""Attributes"":""Directory, ReparsePoint""}", options);
            Assert.Equal(FileAttributes.Directory | FileAttributes.ReparsePoint, state.Attributes);
            FileAttributes attributes = JsonSerializer.Deserialize<FileAttributes>(@"""Normal""", options);
            Assert.Equal(FileAttributes.Normal, attributes);
            attributes = JsonSerializer.Deserialize<FileAttributes>(@"""System, SparseFile""", options);
            Assert.Equal(FileAttributes.System | FileAttributes.SparseFile, attributes);

            // We are case insensitive on read
            attributes = JsonSerializer.Deserialize<FileAttributes>(@"""OFFLINE""", options);
            Assert.Equal(FileAttributes.Offline, attributes);
            attributes = JsonSerializer.Deserialize<FileAttributes>(@"""compressed, notcontentindexed""", options);
            Assert.Equal(FileAttributes.Compressed | FileAttributes.NotContentIndexed, attributes);

            // Numbers are cool by default
            attributes = JsonSerializer.Deserialize<FileAttributes>(@"131072", options);
            Assert.Equal(FileAttributes.NoScrubData, attributes);
            attributes = JsonSerializer.Deserialize<FileAttributes>(@"3", options);
            Assert.Equal(FileAttributes.Hidden | FileAttributes.ReadOnly, attributes);

            string json = JsonSerializer.Serialize(FileAttributes.Hidden, options);
            Assert.Equal(@"""Hidden""", json);
            json = JsonSerializer.Serialize(FileAttributes.Temporary | FileAttributes.Offline, options);
            Assert.Equal(@"""Temporary, Offline""", json);

            // Try a unique casing
            options = CreateStringEnumOptionsForType<FileAttributes>(useGenericVariant, new ToLowerNamingPolicy());

            json = JsonSerializer.Serialize(FileAttributes.NoScrubData, options);
            Assert.Equal(@"""noscrubdata""", json);
            json = JsonSerializer.Serialize(FileAttributes.System | FileAttributes.Offline, options);
            Assert.Equal(@"""system, offline""", json);

            // Undefined values should come out as a number (not a string)
            json = JsonSerializer.Serialize((FileAttributes)(-1), options);
            Assert.Equal(@"-1", json);

            options = CreateStringEnumOptionsForType<FileAttributes>(useGenericVariant, allowIntegerValues: false);
            // Not permitting integers should throw
            Assert.Throws<JsonException>(() => JsonSerializer.Serialize((FileAttributes)(-1), options));

            // Numbers should throw
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>("1", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>("-1", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>(@"""1""", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>(@"""+1""", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>(@"""-1""", options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>(@""" 1 """, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>(@""" +1 """, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<FileAttributes>(@""" -1 """, options));

            attributes = JsonSerializer.Deserialize<FileAttributes>(@"""ReadOnly""", options);
            Assert.Equal(FileAttributes.ReadOnly, attributes);

            // Flag values honor naming policy correctly
            options = CreateStringEnumOptionsForType<FileAttributes>(useGenericVariant, JsonNamingPolicy.SnakeCaseLower);

            json = JsonSerializer.Serialize(
                FileAttributes.Directory | FileAttributes.Compressed | FileAttributes.IntegrityStream,
                options);
            Assert.Equal(@"""directory, compressed, integrity_stream""", json);

            json = JsonSerializer.Serialize((FileAttributes)(-1), options);
            Assert.Equal(@"-1", json);

            json = JsonSerializer.Serialize(FileAttributes.Directory & FileAttributes.Compressed | FileAttributes.IntegrityStream, options);
            Assert.Equal(@"""integrity_stream""", json);
        }

        public class FileState
        {
            public FileAttributes Attributes { get; set; }
        }

        public class Week
        {
            [JsonConverter(typeof(JsonStringEnumConverter))]
            public DayOfWeek WorkStart { get; set; }
            public DayOfWeek WorkEnd { get; set; }
            [JsonConverter(typeof(LowerCaseEnumConverter))]
            public DayOfWeek WeekEnd { get; set; }

            [JsonConverter(typeof(JsonStringEnumConverter<DayOfWeek>))]
            public DayOfWeek WorkStart2 { get; set; }
            [JsonConverter(typeof(LowerCaseEnumConverter<DayOfWeek>))]
            public DayOfWeek WeekEnd2 { get; set; }
        }

        private class LowerCaseEnumConverter : JsonStringEnumConverter
        {
            public LowerCaseEnumConverter() : base(new ToLowerNamingPolicy())
            {
            }
        }

        private class LowerCaseEnumConverter<TEnum> : JsonStringEnumConverter<TEnum>
            where TEnum : struct, Enum
        {
            public LowerCaseEnumConverter() : base(new ToLowerNamingPolicy())
            {
            }
        }

        [Fact]
        public void ConvertEnumUsingAttributes()
        {
            Week week = new Week {
                WorkStart = DayOfWeek.Monday,
                WorkEnd = DayOfWeek.Friday,
                WeekEnd = DayOfWeek.Saturday,
                WorkStart2 = DayOfWeek.Tuesday,
                WeekEnd2 = DayOfWeek.Thursday,
            };

            string json = JsonSerializer.Serialize(week);
            Assert.Equal("""{"WorkStart":"Monday","WorkEnd":5,"WeekEnd":"saturday","WorkStart2":"Tuesday","WeekEnd2":"thursday"}""", json);

            week = JsonSerializer.Deserialize<Week>(json);
            Assert.Equal(DayOfWeek.Monday, week.WorkStart);
            Assert.Equal(DayOfWeek.Friday, week.WorkEnd);
            Assert.Equal(DayOfWeek.Saturday, week.WeekEnd);
            Assert.Equal(DayOfWeek.Tuesday, week.WorkStart2);
            Assert.Equal(DayOfWeek.Thursday, week.WeekEnd2);
        }

        [Fact]
        public void EnumConverterComposition()
        {
            JsonSerializerOptions options = new JsonSerializerOptions { Converters = { new NoFlagsStringEnumConverter() } };
            string json = JsonSerializer.Serialize(DayOfWeek.Monday, options);
            Assert.Equal(@"""Monday""", json);
            json = JsonSerializer.Serialize(FileAccess.Read);
            Assert.Equal(@"1", json);
        }

        public class NoFlagsStringEnumConverter : JsonConverterFactory
        {
            private static JsonStringEnumConverter s_stringEnumConverter = new JsonStringEnumConverter();

            public override bool CanConvert(Type typeToConvert)
                => typeToConvert.IsEnum && !typeToConvert.IsDefined(typeof(FlagsAttribute), inherit: false);

            public override JsonConverter CreateConverter(Type typeToConvert, JsonSerializerOptions options)
                => s_stringEnumConverter.CreateConverter(typeToConvert, options);
        }

        [JsonConverter(typeof(JsonStringEnumConverter))]
        private enum MyCustomEnum
        {
            First = 1,
            Second = 2
        }

        [JsonConverter(typeof(JsonStringEnumConverter<MyCustomEnum2>))]
        private enum MyCustomEnum2
        {
            First = 1,
            Second = 2
        }

        [Theory]
        [InlineData(typeof(MyCustomEnum), MyCustomEnum.Second, "\"Second\"", "2")]
        [InlineData(typeof(MyCustomEnum2), MyCustomEnum2.Second, "\"Second\"", "2")]
        public void EnumWithConverterAttribute(Type enumType, object value, string expectedJson, string alternativeJson)
        {
            string json = JsonSerializer.Serialize(value, enumType);
            Assert.Equal(expectedJson, json);

            object? result = JsonSerializer.Deserialize(json, enumType);
            Assert.Equal(value, result);

            result = JsonSerializer.Deserialize(alternativeJson, enumType);
            Assert.Equal(value, result);
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public static void EnumWithNoValues(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<EmptyEnum>(useGenericVariant);

            Assert.Equal("-1", JsonSerializer.Serialize((EmptyEnum)(-1), options));
            Assert.Equal("1", JsonSerializer.Serialize((EmptyEnum)(1), options));
        }

        public enum EmptyEnum { };

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public static void MoreThan64EnumValuesToSerialize(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<MyEnum>(useGenericVariant);

            for (int i = 0; i < 128; i++)
            {
                MyEnum value = (MyEnum)i;
                string asStr = value.ToString();
                string expected = char.IsLetter(asStr[0]) ? $@"""{asStr}""" : asStr;
                Assert.Equal(expected, JsonSerializer.Serialize(value, options));
            }
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public static void MoreThan64EnumValuesToSerializeWithNamingPolicy(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<MyEnum>(useGenericVariant, new ToLowerNamingPolicy());

            for (int i = 0; i < 128; i++)
            {
                MyEnum value = (MyEnum)i;
                string asStr = value.ToString().ToLowerInvariant();
                string expected = char.IsLetter(asStr[0]) ? $@"""{asStr}""" : asStr;
                Assert.Equal(expected, JsonSerializer.Serialize(value, options));
            }
        }

        [ConditionalFact(typeof(PlatformDetection), nameof(PlatformDetection.IsThreadingSupported))]
        [OuterLoop]
        public static void VeryLargeAmountOfEnumsToSerialize()
        {
            // Ensure we don't throw OutOfMemoryException.

            var options = new JsonSerializerOptions
            {
                Converters = { new JsonStringEnumConverter() }
            };

            const int MaxValue = 2097152; // value for MyEnum.V

            // Every value between 0 and MaxValue maps to a valid enum
            // identifier, and is a candidate to go into the name cache.

            // Write the first 45 values.
            for (int i = 1; i < 46; i++)
            {
                JsonSerializer.Serialize((MyEnum)i, options);
            }

            // At this point, there are 60 values in the name cache;
            // 22 cached at warm-up, the rest in the above loop.

            // Ensure the approximate size limit for the name cache (a concurrent dictionary) is honored.
            // Use multiple threads to perhaps go over the soft limit of 64, but not by more than a couple.
            Parallel.For(0, 8, i => JsonSerializer.Serialize((MyEnum)(46 + i), options));

            // Write the remaining enum values. The cache is capped to avoid
            // OutOfMemoryException due to having too many cached items.
            for (int i = 54; i <= MaxValue; i++)
            {
                JsonSerializer.Serialize((MyEnum)i, options);
            }
        }

        [Flags]
        public enum MyEnum
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
            H = 1 << 7,
            I = 1 << 8,
            J = 1 << 9,
            K = 1 << 10,
            L = 1 << 11,
            M = 1 << 12,
            N = 1 << 13,
            O = 1 << 14,
            P = 1 << 15,
            Q = 1 << 16,
            R = 1 << 17,
            S = 1 << 18,
            T = 1 << 19,
            U = 1 << 20,
            V = 1 << 21,
        }

        [Fact, OuterLoop]
        [ActiveIssue("https://github.com/dotnet/runtime/issues/42677", platforms: TestPlatforms.Windows, runtimes: TestRuntimes.Mono)]
        public static void VeryLargeAmountOfEnumDictionaryKeysToSerialize()
        {
            // Ensure we don't throw OutOfMemoryException.

            const int MaxValue = (int)MyEnum.V;

            // Every value between 0 and MaxValue maps to a valid enum
            // identifier, and is a candidate to go into the name cache.

            // Write the first 45 values.
            Dictionary<MyEnum, int> dictionary;
            for (int i = 1; i < 46; i++)
            {
                dictionary = new Dictionary<MyEnum, int> { { (MyEnum)i, i } };
                JsonSerializer.Serialize(dictionary);
            }

            // At this point, there are 60 values in the name cache;
            // 22 cached at warm-up, the rest in the above loop.

            // Ensure the approximate size limit for the name cache (a concurrent dictionary) is honored.
            // Use multiple threads to perhaps go over the soft limit of 64, but not by more than a couple.
            Parallel.For(
                0,
                8,
                i =>
                {
                    dictionary = new Dictionary<MyEnum, int> { { (MyEnum)(46 + i), i } };
                    JsonSerializer.Serialize(dictionary);
                }
            );

            // Write the remaining enum values. The cache is capped to avoid
            // OutOfMemoryException due to having too many cached items.
            for (int i = 54; i <= MaxValue; i++)
            {
                dictionary = new Dictionary<MyEnum, int> { { (MyEnum)i, i } };
                JsonSerializer.Serialize(dictionary);
            }
        }

        [SkipOnTargetFramework(TargetFrameworkMonikers.NetFramework)]
        [ConditionalFact(typeof(RemoteExecutor), nameof(RemoteExecutor.IsSupported))]
        public static void NegativeEnumValue_CultureInvariance()
        {
            // Regression test for https://github.com/dotnet/runtime/issues/68600
            RemoteExecutor.Invoke(static () =>
            {
                SampleEnumInt32 value = (SampleEnumInt32)(-2);
                string expectedJson = "-2";

                var options = new JsonSerializerOptions
                {
                    Converters = { new JsonStringEnumConverter(allowIntegerValues: true) },
                };

                // Sets the minus sign to -
                CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;

                string actualJson = JsonSerializer.Serialize(value, options);
                Assert.Equal(expectedJson, actualJson);
                SampleEnumInt32 result = JsonSerializer.Deserialize<SampleEnumInt32>(actualJson, options);
                Assert.Equal(value, result);

                // Sets the minus sign to U+2212
                CultureInfo.CurrentCulture = new CultureInfo("sv-SE");

                actualJson = JsonSerializer.Serialize(value, options);
                Assert.Equal(expectedJson, actualJson);
                result = JsonSerializer.Deserialize<SampleEnumInt32>(actualJson, options);
                Assert.Equal(value, result);
            }).Dispose();
        }

        public abstract class NumericEnumKeyDictionaryBase<T>
        {
            public abstract Dictionary<T, int> BuildDictionary(int i);

            [Fact]
            public void SerilizeDictionaryWhenCacheIsFull()
            {
                Dictionary<T, int> dictionary;
                for (int i = 1; i <= 64; i++)
                {
                    dictionary = BuildDictionary(i);
                    JsonSerializer.Serialize(dictionary);
                }

                dictionary = BuildDictionary(0);
                string json = JsonSerializer.Serialize(dictionary);
                Assert.Equal($"{{\"0\":0}}", json);
            }
        }

        public class Int32EnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumInt32>
        {
            public override Dictionary<SampleEnumInt32, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumInt32, int> { { (SampleEnumInt32)i, i } };
        }

        public class UInt32EnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumUInt32>
        {
            public override Dictionary<SampleEnumUInt32, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumUInt32, int> { { (SampleEnumUInt32)i, i } };
        }

        public class UInt64EnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumUInt64>
        {
            public override Dictionary<SampleEnumUInt64, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumUInt64, int> { { (SampleEnumUInt64)i, i } };
        }

        public class Int64EnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumInt64>
        {
            public override Dictionary<SampleEnumInt64, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumInt64, int> { { (SampleEnumInt64)i, i } };
        }

        public class Int16EnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumInt16>
        {
            public override Dictionary<SampleEnumInt16, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumInt16, int> { { (SampleEnumInt16)i, i } };
        }

        public class UInt16EnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumUInt16>
        {
            public override Dictionary<SampleEnumUInt16, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumUInt16, int> { { (SampleEnumUInt16)i, i } };
        }

        public class ByteEnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumByte>
        {
            public override Dictionary<SampleEnumByte, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumByte, int> { { (SampleEnumByte)i, i } };
        }

        public class SByteEnumDictionary : NumericEnumKeyDictionaryBase<SampleEnumSByte>
        {
            public override Dictionary<SampleEnumSByte, int> BuildDictionary(int i) =>
                new Dictionary<SampleEnumSByte, int> { { (SampleEnumSByte)i, i } };
        }


        [Flags]
        public enum SampleEnumInt32
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumUInt32 : uint
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumUInt64 : ulong
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumInt64 : long
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumInt16 : short
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumUInt16 : ushort
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumByte : byte
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Flags]
        public enum SampleEnumSByte : sbyte
        {
            A = 1 << 0,
            B = 1 << 1,
            C = 1 << 2,
            D = 1 << 3,
            E = 1 << 4,
            F = 1 << 5,
            G = 1 << 6,
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public static void Honor_EnumNamingPolicy_On_Deserialization(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<BindingFlags>(useGenericVariant, JsonNamingPolicy.SnakeCaseLower);

            BindingFlags bindingFlags = JsonSerializer.Deserialize<BindingFlags>(@"""non_public""", options);
            Assert.Equal(BindingFlags.NonPublic, bindingFlags);

            // Flags supported without naming policy.
            bindingFlags = JsonSerializer.Deserialize<BindingFlags>(@"""NonPublic, Public""", options);
            Assert.Equal(BindingFlags.NonPublic | BindingFlags.Public, bindingFlags);

            // Flags supported with naming policy.
            bindingFlags = JsonSerializer.Deserialize<BindingFlags>(@"""static, public""", options);
            Assert.Equal(BindingFlags.Static | BindingFlags.Public, bindingFlags);

            // Null not supported.
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<BindingFlags>("null", options));

            // Null supported for nullable enum.
            Assert.Null(JsonSerializer.Deserialize<BindingFlags?>("null", options));
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public static void EnumDictionaryKeyDeserialization(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<BindingFlags>(useGenericVariant);
            options.DictionaryKeyPolicy = JsonNamingPolicy.SnakeCaseLower;

            // Baseline.
            var dict = JsonSerializer.Deserialize<Dictionary<BindingFlags, int>>(@"{""NonPublic, Public"": 1}", options);
            Assert.Equal(1, dict[BindingFlags.NonPublic | BindingFlags.Public]);

            // DictionaryKeyPolicy not honored for dict key deserialization.
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<Dictionary<BindingFlags, int>>(@"{""NonPublic0, Public0"": 1}", options));

            // EnumConverter naming policy not honored.
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<Dictionary<BindingFlags, int>>(@"{""non_public, static"": 0, ""NonPublic, Public"": 1}", options));
        }

        [Theory]
        [InlineData(false)]
        [InlineData(true)]
        public static void StringEnumWithNamingPolicyKeyDeserialization(bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType<BindingFlags>(useGenericVariant, JsonNamingPolicy.SnakeCaseLower);
            options.DictionaryKeyPolicy = JsonNamingPolicy.KebabCaseUpper;

            // DictionaryKeyPolicy not honored for dict key deserialization.
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<Dictionary<BindingFlags, int>>(@"{""NON-PUBLIC, PUBLIC"": 1}", options));

            // EnumConverter naming policy honored.
            Dictionary<BindingFlags, int> result = JsonSerializer.Deserialize<Dictionary<BindingFlags, int>>(@"{""non_public, static"": 0, ""NonPublic, Public"": 1, ""create_instance"": 2 }", options);
            Assert.Contains(BindingFlags.NonPublic | BindingFlags.Static, result);
            Assert.Contains(BindingFlags.NonPublic | BindingFlags.Public, result);
            Assert.Contains(BindingFlags.CreateInstance, result);
        }

        [Fact]
        public static void EnumDictionaryKeySerialization()
        {
            JsonSerializerOptions options = new()
            {
                DictionaryKeyPolicy = JsonNamingPolicy.SnakeCaseLower
            };

            Dictionary<BindingFlags, int> dict = new()
            {
                [BindingFlags.NonPublic | BindingFlags.Public] = 1,
                [BindingFlags.Static] = 2,
            };

            string expected = @"{
    ""public, non_public"": 1,
    ""static"": 2
}";

            JsonTestHelper.AssertJsonEqual(expected, JsonSerializer.Serialize(dict, options));
        }

        [Theory]
        [InlineData(typeof(SampleEnumByte), true)]
        [InlineData(typeof(SampleEnumByte), false)]
        [InlineData(typeof(SampleEnumSByte), true)]
        [InlineData(typeof(SampleEnumSByte), false)]
        [InlineData(typeof(SampleEnumInt16), true)]
        [InlineData(typeof(SampleEnumInt16), false)]
        [InlineData(typeof(SampleEnumUInt16), true)]
        [InlineData(typeof(SampleEnumUInt16), false)]
        [InlineData(typeof(SampleEnumInt32), true)]
        [InlineData(typeof(SampleEnumInt32), false)]
        [InlineData(typeof(SampleEnumUInt32), true)]
        [InlineData(typeof(SampleEnumUInt32), false)]
        [InlineData(typeof(SampleEnumInt64), true)]
        [InlineData(typeof(SampleEnumInt64), false)]
        [InlineData(typeof(SampleEnumUInt64), true)]
        [InlineData(typeof(SampleEnumUInt64), false)]
        public static void DeserializeNumericStringWithAllowIntegerValuesAsFalse(Type enumType, bool useGenericVariant)
        {
            JsonSerializerOptions options = CreateStringEnumOptionsForType(enumType, useGenericVariant, allowIntegerValues: false);

            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@"""1""", enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@"""+1""", enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@"""-1""", enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@""" 1 """, enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@""" +1 """, enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@""" -1 """, enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@$"""{ulong.MaxValue}""", enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@$""" {ulong.MaxValue} """, enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@$"""+{ulong.MaxValue}""", enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@$""" +{ulong.MaxValue} """, enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@$"""{long.MinValue}""", enumType, options));
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize(@$""" {long.MinValue} """, enumType, options));
        }

        private class ToEnumNumberNamingPolicy<T> : JsonNamingPolicy where T : struct, Enum
        {
            public override string ConvertName(string name) => Enum.TryParse(name, out T value) ? value.ToString("D") : name;
        }

        private class ZeroAppenderPolicy : JsonNamingPolicy
        {
            public override string ConvertName(string name) => name + "0";
        }

        private static JsonSerializerOptions CreateStringEnumOptionsForType(Type enumType, bool useGenericVariant, JsonNamingPolicy? namingPolicy = null, bool allowIntegerValues = true)
        {
            Debug.Assert(enumType.IsEnum);

            return new JsonSerializerOptions
            {
                Converters =
                {
                    useGenericVariant
                        ? (JsonConverter)Activator.CreateInstance(typeof(JsonStringEnumConverter<>).MakeGenericType(enumType), namingPolicy, allowIntegerValues)
                        : new JsonStringEnumConverter(namingPolicy, allowIntegerValues)
                }
            };
        }

        private static JsonSerializerOptions CreateStringEnumOptionsForType<TEnum>(bool useGenericVariant, JsonNamingPolicy? namingPolicy = null, bool allowIntegerValues = true) where TEnum : struct, Enum
        {
            return CreateStringEnumOptionsForType(typeof(TEnum), useGenericVariant, namingPolicy, allowIntegerValues);
        }

        [Theory]
        [InlineData(EnumWithMemberAttributes.Value1, "CustomValue1")]
        [InlineData(EnumWithMemberAttributes.Value2, "CustomValue2")]
        [InlineData(EnumWithMemberAttributes.Value3, "Value3")]
        public static void EnumWithMemberAttributes_StringEnumConverter_SerializesAsExpected(EnumWithMemberAttributes value, string expectedJson)
        {
            string json = JsonSerializer.Serialize(value, s_optionsWithStringEnumConverter);
            Assert.Equal($"\"{expectedJson}\"", json);
            Assert.Equal(value, JsonSerializer.Deserialize<EnumWithMemberAttributes>(json, s_optionsWithStringEnumConverter));
        }

        [Theory]
        [InlineData(EnumWithMemberAttributes.Value1)]
        [InlineData(EnumWithMemberAttributes.Value2)]
        [InlineData(EnumWithMemberAttributes.Value3)]
        public static void EnumWithMemberAttributes_NoStringEnumConverter_SerializesAsNumber(EnumWithMemberAttributes value)
        {
            string json = JsonSerializer.Serialize(value);
            Assert.Equal($"{(int)value}", json);
            Assert.Equal(value, JsonSerializer.Deserialize<EnumWithMemberAttributes>(json));
        }

        [Theory]
        [InlineData(EnumWithMemberAttributes.Value1, "CustomValue1")]
        [InlineData(EnumWithMemberAttributes.Value2, "CustomValue2")]
        [InlineData(EnumWithMemberAttributes.Value3, "value3")]
        public static void EnumWithMemberAttributes_StringEnumConverterWithNamingPolicy_NotAppliedToCustomNames(EnumWithMemberAttributes value, string expectedJson)
        {
            JsonSerializerOptions options = new() { Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) } };

            string json = JsonSerializer.Serialize(value, options);
            Assert.Equal($"\"{expectedJson}\"", json);
            Assert.Equal(value, JsonSerializer.Deserialize<EnumWithMemberAttributes>(json, options));
        }

        [Fact]
        public static void EnumWithMemberAttributes_NamingPolicyAndDictionaryKeyPolicy_NotAppliedToCustomNames()
        {
            JsonSerializerOptions options = new()
            {
                Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
                DictionaryKeyPolicy = JsonNamingPolicy.KebabCaseUpper,
            };

            Dictionary<EnumWithMemberAttributes, EnumWithMemberAttributes[]> value = new()
            {
                [EnumWithMemberAttributes.Value1] = [EnumWithMemberAttributes.Value1, EnumWithMemberAttributes.Value2, EnumWithMemberAttributes.Value3 ],
                [EnumWithMemberAttributes.Value2] = [EnumWithMemberAttributes.Value2 ],
                [EnumWithMemberAttributes.Value3] = [EnumWithMemberAttributes.Value3, EnumWithMemberAttributes.Value1 ],
            };

            string json = JsonSerializer.Serialize(value, options);
            JsonTestHelper.AssertJsonEqual("""
                {
                    "CustomValue1": ["CustomValue1", "CustomValue2", "value3"],
                    "CustomValue2": ["CustomValue2"],
                    "VALUE3": ["value3", "CustomValue1"]
                }
                """, json);
        }

        [Theory]
        [InlineData("\"customvalue1\"")]
        [InlineData("\"CUSTOMVALUE1\"")]
        [InlineData("\"cUSTOMvALUE1\"")]
        [InlineData("\"customvalue2\"")]
        [InlineData("\"CUSTOMVALUE2\"")]
        [InlineData("\"cUSTOMvALUE2\"")]
        public static void EnumWithMemberAttributes_CustomizedValuesAreCaseSensitive(string json)
        {
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<EnumWithMemberAttributes>(json, s_optionsWithStringEnumConverter));
        }

        [Theory]
        [InlineData("\"value3\"", EnumWithMemberAttributes.Value3)]
        [InlineData("\"VALUE3\"", EnumWithMemberAttributes.Value3)]
        [InlineData("\"vALUE3\"", EnumWithMemberAttributes.Value3)]
        public static void EnumWithMemberAttributes_DefaultValuesAreCaseInsensitive(string json, EnumWithMemberAttributes expectedValue)
        {
            EnumWithMemberAttributes value = JsonSerializer.Deserialize<EnumWithMemberAttributes>(json, s_optionsWithStringEnumConverter);
            Assert.Equal(expectedValue, value);
        }

        public enum EnumWithMemberAttributes
        {
            [JsonStringEnumMemberName("CustomValue1")]
            Value1 = 1,
            [JsonStringEnumMemberName("CustomValue2")]
            Value2 = 2,
            Value3 = 3,
        }

        [Theory]
        [InlineData(EnumFlagsWithMemberAttributes.Value1, "A")]
        [InlineData(EnumFlagsWithMemberAttributes.Value2, "B")]
        [InlineData(EnumFlagsWithMemberAttributes.Value3, "C")]
        [InlineData(EnumFlagsWithMemberAttributes.Value4, "Value4")]
        [InlineData(EnumFlagsWithMemberAttributes.Value1 | EnumFlagsWithMemberAttributes.Value2, "A, B")]
        [InlineData(EnumFlagsWithMemberAttributes.Value1 | EnumFlagsWithMemberAttributes.Value2 | EnumFlagsWithMemberAttributes.Value3 | EnumFlagsWithMemberAttributes.Value4, "A, B, C, Value4")]
        public static void EnumFlagsWithMemberAttributes_SerializesAsExpected(EnumFlagsWithMemberAttributes value, string expectedJson)
        {
            string json = JsonSerializer.Serialize(value);
            Assert.Equal($"\"{expectedJson}\"", json);
            Assert.Equal(value, JsonSerializer.Deserialize<EnumFlagsWithMemberAttributes>(json));
        }

        [Fact]
        public static void EnumFlagsWithMemberAttributes_NamingPolicyAndDictionaryKeyPolicy_NotAppliedToCustomNames()
        {
            JsonSerializerOptions options = new()
            {
                Converters = { new JsonStringEnumConverter(JsonNamingPolicy.CamelCase) },
                DictionaryKeyPolicy = JsonNamingPolicy.KebabCaseUpper,
            };

            Dictionary<EnumFlagsWithMemberAttributes, EnumFlagsWithMemberAttributes> value = new()
            {
                [EnumFlagsWithMemberAttributes.Value1] = EnumFlagsWithMemberAttributes.Value1 | EnumFlagsWithMemberAttributes.Value2 |
                                                         EnumFlagsWithMemberAttributes.Value3 | EnumFlagsWithMemberAttributes.Value4,

                [EnumFlagsWithMemberAttributes.Value1 | EnumFlagsWithMemberAttributes.Value4] = EnumFlagsWithMemberAttributes.Value3,
                [EnumFlagsWithMemberAttributes.Value4] = EnumFlagsWithMemberAttributes.Value2,
            };

            string json = JsonSerializer.Serialize(value, options);
            JsonTestHelper.AssertJsonEqual("""
                {
                    "A": "A, B, C, value4",
                    "A, VALUE4": "C",
                    "VALUE4": "B"
                }
                """, json);
        }

        [Theory]
        [InlineData("\"a\"")]
        [InlineData("\"b\"")]
        [InlineData("\"A, b\"")]
        [InlineData("\"A, b, C, Value4\"")]
        [InlineData("\"A, B, c, Value4\"")]
        [InlineData("\"a, b, c, Value4\"")]
        [InlineData("\"c, B, A, Value4\"")]
        public static void EnumFlagsWithMemberAttributes_CustomizedValuesAreCaseSensitive(string json)
        {
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<EnumFlagsWithMemberAttributes>(json));
        }

        [Theory]
        [InlineData("\"value4\"", EnumFlagsWithMemberAttributes.Value4)]
        [InlineData("\"value4, VALUE4\"", EnumFlagsWithMemberAttributes.Value4)]
        [InlineData("\"A, value4, VALUE4, A,B,A,A\"", EnumFlagsWithMemberAttributes.Value1 | EnumFlagsWithMemberAttributes.Value2 | EnumFlagsWithMemberAttributes.Value4)]
        [InlineData("\"VALUE4, VAlUE5\"", EnumFlagsWithMemberAttributes.Value4 | EnumFlagsWithMemberAttributes.Value5)]
        public static void EnumFlagsWithMemberAttributes_DefaultValuesAreCaseInsensitive(string json, EnumFlagsWithMemberAttributes expectedValue)
        {
            EnumFlagsWithMemberAttributes value = JsonSerializer.Deserialize<EnumFlagsWithMemberAttributes>(json);
            Assert.Equal(expectedValue, value);
        }

        [Flags, JsonConverter(typeof(JsonStringEnumConverter<EnumFlagsWithMemberAttributes>))]
        public enum EnumFlagsWithMemberAttributes
        {
            [JsonStringEnumMemberName("A")]
            Value1 = 1,
            [JsonStringEnumMemberName("B")]
            Value2 = 2,
            [JsonStringEnumMemberName("C")]
            Value3 = 4,
            Value4 = 8,
            Value5 = 16,
        }

        [Theory]
        [InlineData(EnumWithConflictingMemberAttributes.Value1)]
        [InlineData(EnumWithConflictingMemberAttributes.Value2)]
        [InlineData(EnumWithConflictingMemberAttributes.Value3)]
        public static void EnumWithConflictingMemberAttributes_IsTolerated(EnumWithConflictingMemberAttributes value)
        {
            string json = JsonSerializer.Serialize(value);
            Assert.Equal("\"Value3\"", json);
            Assert.Equal(EnumWithConflictingMemberAttributes.Value1, JsonSerializer.Deserialize<EnumWithConflictingMemberAttributes>(json));
        }

        [JsonConverter(typeof(JsonStringEnumConverter<EnumWithConflictingMemberAttributes>))]
        public enum EnumWithConflictingMemberAttributes
        {
            [JsonStringEnumMemberName("Value3")]
            Value1 = 1,
            [JsonStringEnumMemberName("Value3")]
            Value2 = 2,
            Value3 = 3,
        }

        [Theory]
        [InlineData(EnumWithConflictingCaseNames.ValueWithConflictingCase, "\"ValueWithConflictingCase\"")]
        [InlineData(EnumWithConflictingCaseNames.VALUEwithCONFLICTINGcase, "\"VALUEwithCONFLICTINGcase\"")]
        [InlineData(EnumWithConflictingCaseNames.Value3, "\"VALUEWITHCONFLICTINGCASE\"")]
        public static void EnumWithConflictingCaseNames_SerializesAsExpected(EnumWithConflictingCaseNames value, string expectedJson)
        {
            string json = JsonSerializer.Serialize(value);
            Assert.Equal(expectedJson, json);
            EnumWithConflictingCaseNames deserializedValue = JsonSerializer.Deserialize<EnumWithConflictingCaseNames>(json);
            Assert.Equal(value, deserializedValue);
        }

        [Theory]
        [InlineData("\"valuewithconflictingcase\"")]
        [InlineData("\"vALUEwITHcONFLICTINGcASE\"")]
        public static void EnumWithConflictingCaseNames_DeserializingMismatchingCaseDefaultsToFirstValue(string json)
        {
            EnumWithConflictingCaseNames value = JsonSerializer.Deserialize<EnumWithConflictingCaseNames>(json);
            Assert.Equal(EnumWithConflictingCaseNames.ValueWithConflictingCase, value);
        }

        [JsonConverter(typeof(JsonStringEnumConverter))]
        public enum EnumWithConflictingCaseNames
        {
            ValueWithConflictingCase = 1,
            VALUEwithCONFLICTINGcase = 2,
            [JsonStringEnumMemberName("VALUEWITHCONFLICTINGCASE")]
            Value3 = 3,
        }

        [Theory]
        [InlineData(EnumWithValidMemberNames.Value1, "\"Intermediate whitespace\\t is allowed\\r\\nin enums\"")]
        [InlineData(EnumWithValidMemberNames.Value2, "\"Including support for commas, and other punctuation.\"")]
        [InlineData(EnumWithValidMemberNames.Value3, "\"Nice \\uD83D\\uDE80\\uD83D\\uDE80\\uD83D\\uDE80\"")]
        [InlineData(EnumWithValidMemberNames.Value4, "\"5\"")]
        [InlineData(EnumWithValidMemberNames.Value1 | EnumWithValidMemberNames.Value4, "5")]
        public static void EnumWithValidMemberNameOverrides(EnumWithValidMemberNames value, string expectedJsonString)
        {
            string json = JsonSerializer.Serialize(value);
            Assert.Equal(expectedJsonString, json);
            Assert.Equal(value, JsonSerializer.Deserialize<EnumWithValidMemberNames>(json));
        }

        [Fact]
        public static void EnumWithNumberIdentifier_CanDeserializeAsUnderlyingValue()
        {
            EnumWithValidMemberNames value = JsonSerializer.Deserialize<EnumWithValidMemberNames>("\"4\"");
            Assert.Equal(EnumWithValidMemberNames.Value4, value);
        }

        [Fact]
        public static void EnumWithNumberIdentifier_NoNumberSupported_FailsWhenDeserializingUnderlyingValue()
        {
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<EnumWithValidMemberNames>("\"4\"", s_optionsWithStringAndNoIntegerEnumConverter));
        }

        [JsonConverter(typeof(JsonStringEnumConverter))]
        public enum EnumWithValidMemberNames
        {
            [JsonStringEnumMemberName("Intermediate whitespace\t is allowed\r\nin enums")]
            Value1 = 1,

            [JsonStringEnumMemberName("Including support for commas, and other punctuation.")]
            Value2 = 2,

            [JsonStringEnumMemberName("Nice 🚀🚀🚀")]
            Value3 = 3,

            [JsonStringEnumMemberName("5")]
            Value4 = 4
        }

        [Theory]
        [InlineData(EnumFlagsWithValidMemberNames.Value1 | EnumFlagsWithValidMemberNames.Value2, "\"Intermediate whitespace\\t is allowed\\r\\nin enums, Including support for some punctuation; except commas.\"")]
        [InlineData(EnumFlagsWithValidMemberNames.Value3 | EnumFlagsWithValidMemberNames.Value4, "\"Nice \\uD83D\\uDE80\\uD83D\\uDE80\\uD83D\\uDE80, 5\"")]
        [InlineData(EnumFlagsWithValidMemberNames.Value4, "\"5\"")]
        public static void EnumFlagsWithValidMemberNameOverrides(EnumFlagsWithValidMemberNames value, string expectedJsonString)
        {
            string json = JsonSerializer.Serialize(value);
            Assert.Equal(expectedJsonString, json);
            Assert.Equal(value, JsonSerializer.Deserialize<EnumFlagsWithValidMemberNames>(json));
        }

        [Theory]
        [InlineData("\"\\r\\n Intermediate whitespace\\t is allowed\\r\\nin enums   ,      Including support for some punctuation; except commas.\\r\\n\"", EnumFlagsWithValidMemberNames.Value1 | EnumFlagsWithValidMemberNames.Value2)]
        [InlineData("\"         5\\t,     \\r\\n      5,\\t          5\"", EnumFlagsWithValidMemberNames.Value4)]
        public static void EnumFlagsWithValidMemberNameOverrides_SupportsWhitespaceSeparatedValues(string json, EnumFlagsWithValidMemberNames expectedValue)
        {
            EnumFlagsWithValidMemberNames result = JsonSerializer.Deserialize<EnumFlagsWithValidMemberNames>(json);
            Assert.Equal(expectedValue, result);
        }

        [Theory]
        [InlineData("\"\"")]
        [InlineData("\"    \\r\\n   \"")]
        [InlineData("\",\"")]
        [InlineData("\",,,\"")]
        [InlineData("\", \\r\\n,,\"")]
        [InlineData("\"\\r\\n Intermediate whitespace\\t is allowed\\r\\nin enums   ,  13 ,    Including support for some punctuation; except commas.\\r\\n\"")]
        [InlineData("\"\\r\\n Intermediate whitespace\\t is allowed\\r\\nin enums   ,  ,    Including support for some punctuation; except commas.\\r\\n\"")]
        [InlineData("\"         5\\t,     \\r\\n , UNKNOWN_IDENTIFIER \r\n,     5,\\t          5\"")]
        public static void EnumFlagsWithValidMemberNameOverrides_FailsOnInvalidJsonValues(string json)
        {
            Assert.Throws<JsonException>(() => JsonSerializer.Deserialize<EnumFlagsWithValidMemberNames>(json));
        }

        [Flags, JsonConverter(typeof(JsonStringEnumConverter))]
        public enum EnumFlagsWithValidMemberNames
        {
            [JsonStringEnumMemberName("Intermediate whitespace\t is allowed\r\nin enums")]
            Value1 = 1,

            [JsonStringEnumMemberName("Including support for some punctuation; except commas.")]
            Value2 = 2,

            [JsonStringEnumMemberName("Nice 🚀🚀🚀")]
            Value3 = 4,

            [JsonStringEnumMemberName("5")]
            Value4 = 8
        }

        [Theory]
        [InlineData(typeof(EnumWithInvalidMemberName1), "")]
        [InlineData(typeof(EnumWithInvalidMemberName2), "")]
        [InlineData(typeof(EnumWithInvalidMemberName3), "   ")]
        [InlineData(typeof(EnumWithInvalidMemberName4), "   HasLeadingWhitespace")]
        [InlineData(typeof(EnumWithInvalidMemberName5), "HasTrailingWhitespace\n")]
        [InlineData(typeof(EnumWithInvalidMemberName6), "Comma separators not allowed, in flags enums")]
        public static void EnumWithInvalidMemberName_Throws(Type enumType, string memberName)
        {
            object value = Activator.CreateInstance(enumType);
            string expectedExceptionMessage = $"Enum type '{enumType.Name}' uses unsupported identifier '{memberName}'.";
            InvalidOperationException ex;

            ex = Assert.Throws<InvalidOperationException>(() => JsonSerializer.Serialize(value, enumType, s_optionsWithStringEnumConverter));
            Assert.Contains(expectedExceptionMessage, ex.Message);

            ex = Assert.Throws<InvalidOperationException>(() => JsonSerializer.Deserialize("\"str\"", enumType, s_optionsWithStringEnumConverter));
            Assert.Contains(expectedExceptionMessage, ex.Message);
        }

        public enum EnumWithInvalidMemberName1
        {
            [JsonStringEnumMemberName(null!)]
            Value
        }

        public enum EnumWithInvalidMemberName2
        {
            [JsonStringEnumMemberName("")]
            Value
        }

        public enum EnumWithInvalidMemberName3
        {
            [JsonStringEnumMemberName("   ")]
            Value
        }

        public enum EnumWithInvalidMemberName4
        {
            [JsonStringEnumMemberName("   HasLeadingWhitespace")]
            Value
        }

        public enum EnumWithInvalidMemberName5
        {
            [JsonStringEnumMemberName("HasTrailingWhitespace\n")]
            Value
        }

        [Flags]
        public enum EnumWithInvalidMemberName6
        {
            [JsonStringEnumMemberName("Comma separators not allowed, in flags enums")]
            Value
        }

        [Theory]
        [InlineData("\"cAmElCaSe\"", EnumWithVaryingNamingPolicies.camelCase, JsonKnownNamingPolicy.SnakeCaseUpper)]
        [InlineData("\"cAmElCaSe\"", EnumWithVaryingNamingPolicies.camelCase, JsonKnownNamingPolicy.SnakeCaseLower)]
        [InlineData("\"cAmElCaSe\"", EnumWithVaryingNamingPolicies.camelCase, JsonKnownNamingPolicy.KebabCaseUpper)]
        [InlineData("\"cAmElCaSe\"", EnumWithVaryingNamingPolicies.camelCase, JsonKnownNamingPolicy.KebabCaseLower)]
        [InlineData("\"pAsCaLcAsE\"", EnumWithVaryingNamingPolicies.PascalCase, JsonKnownNamingPolicy.SnakeCaseUpper)]
        [InlineData("\"pAsCaLcAsE\"", EnumWithVaryingNamingPolicies.PascalCase, JsonKnownNamingPolicy.SnakeCaseLower)]
        [InlineData("\"pAsCaLcAsE\"", EnumWithVaryingNamingPolicies.PascalCase, JsonKnownNamingPolicy.KebabCaseUpper)]
        [InlineData("\"pAsCaLcAsE\"", EnumWithVaryingNamingPolicies.PascalCase, JsonKnownNamingPolicy.KebabCaseLower)]
        [InlineData("\"sNaKe_CaSe_UpPeR\"", EnumWithVaryingNamingPolicies.SNAKE_CASE_UPPER, JsonKnownNamingPolicy.SnakeCaseUpper)]
        [InlineData("\"sNaKe_CaSe_LoWeR\"", EnumWithVaryingNamingPolicies.snake_case_lower, JsonKnownNamingPolicy.SnakeCaseLower)]
        [InlineData("\"cAmElCaSe\"", EnumWithVaryingNamingPolicies.camelCase, JsonKnownNamingPolicy.CamelCase)]
        [InlineData("\"a\"", EnumWithVaryingNamingPolicies.A, JsonKnownNamingPolicy.CamelCase)]
        [InlineData("\"a\"", EnumWithVaryingNamingPolicies.A, JsonKnownNamingPolicy.SnakeCaseUpper)]
        [InlineData("\"a\"", EnumWithVaryingNamingPolicies.A, JsonKnownNamingPolicy.SnakeCaseLower)]
        [InlineData("\"a\"", EnumWithVaryingNamingPolicies.A, JsonKnownNamingPolicy.KebabCaseUpper)]
        [InlineData("\"a\"", EnumWithVaryingNamingPolicies.A, JsonKnownNamingPolicy.KebabCaseLower)]
        [InlineData("\"B\"", EnumWithVaryingNamingPolicies.b, JsonKnownNamingPolicy.CamelCase)]
        [InlineData("\"B\"", EnumWithVaryingNamingPolicies.b, JsonKnownNamingPolicy.SnakeCaseUpper)]
        [InlineData("\"B\"", EnumWithVaryingNamingPolicies.b, JsonKnownNamingPolicy.SnakeCaseLower)]
        [InlineData("\"B\"", EnumWithVaryingNamingPolicies.b, JsonKnownNamingPolicy.KebabCaseUpper)]
        [InlineData("\"B\"", EnumWithVaryingNamingPolicies.b, JsonKnownNamingPolicy.KebabCaseLower)]
        public static void StringConverterWithNamingPolicyIsCaseInsensitive(string json, EnumWithVaryingNamingPolicies expectedValue, JsonKnownNamingPolicy namingPolicy)
        {
            JsonNamingPolicy policy = namingPolicy switch
            {
                JsonKnownNamingPolicy.CamelCase => JsonNamingPolicy.CamelCase,
                JsonKnownNamingPolicy.SnakeCaseLower => JsonNamingPolicy.SnakeCaseLower,
                JsonKnownNamingPolicy.SnakeCaseUpper => JsonNamingPolicy.SnakeCaseUpper,
                JsonKnownNamingPolicy.KebabCaseLower => JsonNamingPolicy.KebabCaseLower,
                JsonKnownNamingPolicy.KebabCaseUpper => JsonNamingPolicy.KebabCaseUpper,
                _ => throw new ArgumentOutOfRangeException(nameof(namingPolicy)),
            };

            JsonSerializerOptions options = new() { Converters = { new JsonStringEnumConverter(policy) } };

            EnumWithVaryingNamingPolicies value = JsonSerializer.Deserialize<EnumWithVaryingNamingPolicies>(json, options);
            Assert.Equal(expectedValue, value);
        }

        public enum EnumWithVaryingNamingPolicies
        {
            SNAKE_CASE_UPPER,
            snake_case_lower,
            camelCase,
            PascalCase,
            A,
            b,
        }

        [Fact]
        public static void EnumWithOverlappingBitsTests()
        {
            JsonSerializerOptions options = new() { Converters = { new JsonStringEnumConverter() } };

            EnumWithOverlappingBits e1 = EnumWithOverlappingBits.BITS01 | EnumWithOverlappingBits.BIT3;
            string json1 = JsonSerializer.Serialize(e1, options);
            Assert.Equal("\"BITS01, BIT3\"", json1);

            EnumWithOverlappingBits2 e2 = EnumWithOverlappingBits2.BITS01 | EnumWithOverlappingBits2.BIT3;
            string json2 = JsonSerializer.Serialize(e2, options);
            Assert.Equal("\"BITS01, BIT3\"", json2);
        }

        [Flags]
        public enum EnumWithOverlappingBits
        {
            UNKNOWN = 0,
            BIT0 = 1,
            BIT1 = 2,
            BIT2 = 4,
            BIT3 = 8,
            BITS01 = 3,
        }

        [Flags]
        public enum EnumWithOverlappingBits2
        {
            UNKNOWN = 0,
            BIT0 = 1,
            // direct option for bit 1 missing
            BIT2 = 4,
            BIT3 = 8,
            BITS01 = 3,
        }
    }
}
