// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Buffers;
using System.Collections;
using System.Globalization;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using System.Collections.Generic;
using System.IO;
using Xunit;
using System.Buffers.Text;
using System.IO.Tests;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Threading;
using TestData = System.Text.Json.Serialization.Tests.TestData;

namespace System.Text.Json.Tests
{
    public static class JsonDocumentTests
    {
        private static readonly byte[] Utf8Bom = { 0xEF, 0xBB, 0xBF };

        private static readonly JsonDocumentOptions s_noDuplicateParamsOptions = new() { AllowDuplicateProperties = false };

        private static readonly Dictionary<TestCaseType, string> s_expectedConcat =
            new Dictionary<TestCaseType, string>();

        private static readonly Dictionary<TestCaseType, string> s_compactJson =
            new Dictionary<TestCaseType, string>();

        public static IEnumerable<object[]> BadBOMCases { get; } =
            new object[][]
            {
                new object[] { "\u00EF" },
                new object[] { "\u00EF1" },
                new object[] { "\u00EF\u00BB" },
                new object[] { "\u00EF\u00BB1" },
                new object[] { "\u00EF\u00BB\u00BE" },
                new object[] { "\u00EF\u00BB\u00BE1" },
                new object[] { "\u00EF\u00BB\u00FB" },
                new object[] { "\u00EF\u00BB\u00FB1" },

                // Legal BOM, but no payload.
                new object[] { "\u00EF\u00BB\u00BF" },
            };

        public static IEnumerable<object[]> ReducedTestCases { get; } =
            new List<object[]>
            {
                new object[] { true, TestCaseType.ProjectLockJson, SR.ProjectLockJson},
                new object[] { true, TestCaseType.Json40KB, SR.Json40KB},
                new object[] { false, TestCaseType.DeepTree, SR.DeepTree},
                new object[] { false, TestCaseType.Json400KB, SR.Json400KB},
            };

        public static IEnumerable<object[]> TestCases { get; } =
            new List<object[]>
            {
                new object[] { true, TestCaseType.Basic, SR.BasicJson},
                new object[] { true, TestCaseType.BasicLargeNum, SR.BasicJsonWithLargeNum}, // Json.NET treats numbers starting with 0 as octal (0425 becomes 277)
                new object[] { true, TestCaseType.BroadTree, SR.BroadTree}, // \r\n behavior is different between Json.NET and System.Text.Json
                new object[] { true, TestCaseType.DeepTree, SR.DeepTree},
                new object[] { true, TestCaseType.FullSchema1, SR.FullJsonSchema1},
                new object[] { true, TestCaseType.HelloWorld, SR.HelloWorld},
                new object[] { true, TestCaseType.LotsOfNumbers, SR.LotsOfNumbers},
                new object[] { true, TestCaseType.LotsOfStrings, SR.LotsOfStrings},
                new object[] { true, TestCaseType.ProjectLockJson, SR.ProjectLockJson},
                new object[] { true, TestCaseType.Json400B, SR.Json400B},
                new object[] { true, TestCaseType.Json4KB, SR.Json4KB},
                new object[] { true, TestCaseType.Json40KB, SR.Json40KB},
                new object[] { true, TestCaseType.Json400KB, SR.Json400KB},

                new object[] { false, TestCaseType.Basic, SR.BasicJson},
                new object[] { false, TestCaseType.BasicLargeNum, SR.BasicJsonWithLargeNum}, // Json.NET treats numbers starting with 0 as octal (0425 becomes 277)
                new object[] { false, TestCaseType.BroadTree, SR.BroadTree}, // \r\n behavior is different between Json.NET and System.Text.Json
                new object[] { false, TestCaseType.DeepTree, SR.DeepTree},
                new object[] { false, TestCaseType.FullSchema1, SR.FullJsonSchema1},
                new object[] { false, TestCaseType.HelloWorld, SR.HelloWorld},
                new object[] { false, TestCaseType.LotsOfNumbers, SR.LotsOfNumbers},
                new object[] { false, TestCaseType.LotsOfStrings, SR.LotsOfStrings},
                new object[] { false, TestCaseType.ProjectLockJson, SR.ProjectLockJson},
                new object[] { false, TestCaseType.Json400B, SR.Json400B},
                new object[] { false, TestCaseType.Json4KB, SR.Json4KB},
                new object[] { false, TestCaseType.Json40KB, SR.Json40KB},
                new object[] { false, TestCaseType.Json400KB, SR.Json400KB},
            };

        // TestCaseType is only used to give the json strings a descriptive name within the unit tests.
        public enum TestCaseType
        {
            HelloWorld,
            Basic,
            BasicLargeNum,
            ProjectLockJson,
            FullSchema1,
            DeepTree,
            BroadTree,
            LotsOfNumbers,
            LotsOfStrings,
            Json400B,
            Json4KB,
            Json40KB,
            Json400KB,
        }

        public enum ParseReaderScenario
        {
            NotStarted,
            StartAtFirstToken,
            StartAtNestedValue,
            StartAtPropertyName,
        }

        public static IEnumerable<object[]> ParseReaderSuccessfulCases { get; } = BuildParseReaderSuccessfulCases().ToList();

        private static IEnumerable<object[]> BuildParseReaderSuccessfulCases()
        {
            var scenarios = (ParseReaderScenario[])Enum.GetValues(typeof(ParseReaderScenario));

            foreach (int segmentCount in new[] { 0, 1, 2, 15, 125 })
            {
                foreach (ParseReaderScenario scenario in scenarios)
                {
                    yield return new object[] { scenario, segmentCount };
                }
            }
        }

        private static string ReadHelloWorld(JToken obj)
        {
            string message = (string)obj["message"];
            return message;
        }

        private static string ReadJson400KB(JToken obj)
        {
            var sb = new StringBuilder(250000);
            foreach (JToken token in obj)
            {
                sb.Append((string)token["_id"]);
                sb.Append((int)token["index"]);
                sb.Append((string)token["guid"]);
                sb.Append((bool)token["isActive"]);
                sb.Append((string)token["balance"]);
                sb.Append((string)token["picture"]);
                sb.Append((int)token["age"]);
                sb.Append((string)token["eyeColor"]);
                sb.Append((string)token["name"]);
                sb.Append((string)token["gender"]);
                sb.Append((string)token["company"]);
                sb.Append((string)token["email"]);
                sb.Append((string)token["phone"]);
                sb.Append((string)token["address"]);
                sb.Append((string)token["about"]);
                sb.Append((string)token["registered"]);
                sb.Append((double)token["latitude"]);
                sb.Append((double)token["longitude"]);

                JToken tags = token["tags"];
                foreach (JToken tag in tags)
                {
                    sb.Append((string)tag);
                }
                JToken friends = token["friends"];
                foreach (JToken friend in friends)
                {
                    sb.Append((int)friend["id"]);
                    sb.Append((string)friend["name"]);
                }
                sb.Append((string)token["greeting"]);
                sb.Append((string)token["favoriteFruit"]);

            }
            return sb.ToString();
        }

        private static string ReadHelloWorld(JsonElement obj)
        {
            string message = obj.GetProperty("message").GetString();
            return message;
        }

        private static string ReadJson400KB(JsonElement obj)
        {
            var sb = new StringBuilder(250000);

            foreach (JsonElement element in obj.EnumerateArray())
            {
                sb.Append(element.GetProperty("_id").GetString());
                sb.Append(element.GetProperty("index").GetInt32());
                sb.Append(element.GetProperty("guid").GetString());
                sb.Append(element.GetProperty("isActive").GetBoolean());
                sb.Append(element.GetProperty("balance").GetString());
                sb.Append(element.GetProperty("picture").GetString());
                sb.Append(element.GetProperty("age").GetInt32());
                sb.Append(element.GetProperty("eyeColor").GetString());
                sb.Append(element.GetProperty("name").GetString());
                sb.Append(element.GetProperty("gender").GetString());
                sb.Append(element.GetProperty("company").GetString());
                sb.Append(element.GetProperty("email").GetString());
                sb.Append(element.GetProperty("phone").GetString());
                sb.Append(element.GetProperty("address").GetString());
                sb.Append(element.GetProperty("about").GetString());
                sb.Append(element.GetProperty("registered").GetString());
                sb.Append(element.GetProperty("latitude").GetDouble());
                sb.Append(element.GetProperty("longitude").GetDouble());

                JsonElement tags = element.GetProperty("tags");
                for (int j = 0; j < tags.GetArrayLength(); j++)
                {
                    sb.Append(tags[j].GetString());
                }
                JsonElement friends = element.GetProperty("friends");
                for (int j = 0; j < friends.GetArrayLength(); j++)
                {
                    sb.Append(friends[j].GetProperty("id").GetInt32());
                    sb.Append(friends[j].GetProperty("name").GetString());
                }
                sb.Append(element.GetProperty("greeting").GetString());
                sb.Append(element.GetProperty("favoriteFruit").GetString());
            }

            return sb.ToString();
        }

        [Theory]
        // The ReadOnlyMemory<bytes> variant is the only one that runs all the input documents.
        // The rest use a reduced set as because (implementation detail) they ultimately
        // funnel into the same worker code and the difference between Reduced and Full
        // is about 0.7 seconds (which adds up).
        //
        // If the internals change such that one of these is exercising substantially different
        // code, then it should switch to the full variation set.
        [MemberData(nameof(TestCases))]
        public static async Task ParseJson_MemoryBytes(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => Task.FromResult(JsonDocument.Parse(bytes.AsMemory())));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_String(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                str => Task.FromResult(JsonDocument.Parse(str)),
                null);
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_SeekableStream(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => Task.FromResult(JsonDocument.Parse(new MemoryStream(bytes))));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_SeekableStream_Async(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => JsonDocument.ParseAsync(new MemoryStream(bytes)));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_UnseekableStream(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => JsonDocument.ParseAsync(
                    new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, bytes)));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_UnseekableStream_Async(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => JsonDocument.ParseAsync(new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, bytes)));
        }


        [Fact]
        public static void ParseJson_SeekableStream_Small()
        {
            byte[] data = { (byte)'1', (byte)'1' };

            using (JsonDocument doc = JsonDocument.Parse(new MemoryStream(data)))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Number, root.ValueKind);
                Assert.Equal(11, root.GetInt32());
            }
        }

        [Fact]
        public static void ParseJson_UnseekableStream_Small()
        {
            byte[] data = { (byte)'1', (byte)'1' };

            using (JsonDocument doc =
                JsonDocument.Parse(new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, data: data)))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Number, root.ValueKind);
                Assert.Equal(11, root.GetInt32());
            }
        }

        [Fact]
        public static async Task ParseJson_SeekableStream_Small_Async()
        {
            byte[] data = { (byte)'1', (byte)'1' };

            using (JsonDocument doc = await JsonDocument.ParseAsync(new MemoryStream(data)))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Number, root.ValueKind);
                Assert.Equal(11, root.GetInt32());
            }
        }

        [Fact]
        public static async Task ParseJson_UnseekableStream_Small_Async()
        {
            byte[] data = { (byte)'1', (byte)'1' };

            using (JsonDocument doc = await JsonDocument.ParseAsync(
                new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, data: data)))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Number, root.ValueKind);
                Assert.Equal(11, root.GetInt32());
            }
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_SeekableStream_WithBOM(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => Task.FromResult(JsonDocument.Parse(new MemoryStream(Utf8Bom.Concat(bytes).ToArray()))));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_SeekableStream_Async_WithBOM(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => JsonDocument.ParseAsync(new MemoryStream(Utf8Bom.Concat(bytes).ToArray())));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_UnseekableStream_WithBOM(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => Task.FromResult(JsonDocument.Parse(
                    new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, Utf8Bom.Concat(bytes).ToArray()))));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_UnseekableStream_Async_WithBOM(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => JsonDocument.ParseAsync(
                        new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, Utf8Bom.Concat(bytes).ToArray())));
        }

        [Fact]
        public static void ParseJson_Stream_ClearRentedBuffer_WhenThrow_CodeCoverage()
        {
            using (Stream stream = new ThrowOnReadStream(new byte[] { 1 }))
            {
                Assert.Throws<EndOfStreamException>(() => JsonDocument.Parse(stream));
            }
        }

        [Fact]
        public static async Task ParseJson_Stream_Async_ClearRentedBuffer_WhenThrow_CodeCoverage()
        {
            using (Stream stream = new ThrowOnReadStream(new byte[] { 1 }))
            {
                await Assert.ThrowsAsync<EndOfStreamException>(async () => await JsonDocument.ParseAsync(stream));
            }
        }

        [Fact]
        public static void ParseJson_Stream_ThrowsOn_ArrayPoolRent_CodeCoverage()
        {
            using (Stream stream = new ThrowOnCanSeekStream(new byte[] { 1 }))
            {
                Assert.Throws<InsufficientMemoryException>(() => JsonDocument.Parse(stream));
            }
        }

        [Fact]
        public static async Task ParseJson_Stream_Async_ThrowsOn_ArrayPoolRent_CodeCoverage()
        {
            using (Stream stream = new ThrowOnCanSeekStream(new byte[] { 1 }))
            {
                await Assert.ThrowsAsync<InsufficientMemoryException>(async () => await JsonDocument.ParseAsync(stream));
            }
        }

        [Theory]
        [MemberData(nameof(BadBOMCases))]
        public static void ParseJson_SeekableStream_BadBOM(string json)
        {
            byte[] data = Encoding.UTF8.GetBytes(json);
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(new MemoryStream(data)));
        }

        [Theory]
        [MemberData(nameof(BadBOMCases))]
        public static Task ParseJson_SeekableStream_Async_BadBOM(string json)
        {
            byte[] data = Encoding.UTF8.GetBytes(json);
            return Assert.ThrowsAnyAsync<JsonException>(() => JsonDocument.ParseAsync(new MemoryStream(data)));
        }

        [Theory]
        [MemberData(nameof(BadBOMCases))]
        public static void ParseJson_UnseekableStream_BadBOM(string json)
        {
            byte[] data = Encoding.UTF8.GetBytes(json);

            Assert.ThrowsAny<JsonException>(
                () => JsonDocument.Parse(
                    new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, data)));
        }

        [Theory]
        [MemberData(nameof(BadBOMCases))]
        [SkipOnCoreClr("https://github.com/dotnet/runtime/issues/45464", ~RuntimeConfiguration.Release)]
        public static Task ParseJson_UnseekableStream_Async_BadBOM(string json)
        {
            byte[] data = Encoding.UTF8.GetBytes(json);

            return Assert.ThrowsAnyAsync<JsonException>(
                () => JsonDocument.ParseAsync(
                    new WrappedMemoryStream(canRead: true, canWrite: false, canSeek: false, data)));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_SequenceBytes_Single(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => Task.FromResult(JsonDocument.Parse(new ReadOnlySequence<byte>(bytes))));
        }

        [Theory]
        [MemberData(nameof(ReducedTestCases))]
        public static async Task ParseJson_SequenceBytes_Multi(bool compactData, TestCaseType type, string jsonString)
        {
            await ParseJsonAsync(
                compactData,
                type,
                jsonString,
                null,
                bytes => Task.FromResult(JsonDocument.Parse(JsonTestHelper.SegmentInto(bytes, 31))));
        }

        private static async Task ParseJsonAsync(
            bool compactData,
            TestCaseType type,
            string jsonString,
            Func<string, Task<JsonDocument>> stringDocBuilder,
            Func<byte[], Task<JsonDocument>> bytesDocBuilder)
        {
            // One, but not both, must be null.
            if ((stringDocBuilder == null) == (bytesDocBuilder == null))
                throw new InvalidOperationException();

            // Remove all formatting/indentation
            if (compactData)
            {
                jsonString = GetCompactJson(type, jsonString);
            }

            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = await (stringDocBuilder?.Invoke(jsonString) ?? bytesDocBuilder?.Invoke(dataUtf8)))
            {
                Assert.NotNull(doc);

                JsonElement rootElement = doc.RootElement;

                Func<JToken, string> expectedFunc = null;
                Func<JsonElement, string> actualFunc = null;

                switch (type)
                {
                    case TestCaseType.Json400KB:
                        expectedFunc = token => ReadJson400KB(token);
                        actualFunc = element => ReadJson400KB(element);
                        break;
                    case TestCaseType.HelloWorld:
                        expectedFunc = token => ReadHelloWorld(token);
                        actualFunc = element => ReadHelloWorld(element);
                        break;
                }

                if (expectedFunc != null)
                {
                    string expectedCustom;
                    string actualCustom;

                    using (var stream = new MemoryStream(dataUtf8))
                    using (var streamReader = new StreamReader(stream, Encoding.UTF8, false, 1024, true))
                    using (var jsonReader = new JsonTextReader(streamReader) { MaxDepth = null })
                    {
                        JToken jToken = JToken.ReadFrom(jsonReader);

                        expectedCustom = expectedFunc(jToken);
                        actualCustom = actualFunc(rootElement);
                    }

                    Assert.Equal(expectedCustom, actualCustom);
                }

                string actual = doc.PrintJson();
                string expected = GetExpectedConcat(type, jsonString);

                Assert.Equal(expected, actual);

                Assert.Equal(jsonString, rootElement.GetRawText());
            }
        }

        private static string PrintJson(this JsonDocument document, int sizeHint = 0)
        {
            return PrintJson(document.RootElement, sizeHint);
        }

        private static string PrintJson(this JsonElement element, int sizeHint = 0)
        {
            StringBuilder sb = new StringBuilder(sizeHint);
            DepthFirstAppend(sb, element);
            return sb.ToString();
        }

        private static void DepthFirstAppend(StringBuilder buf, JsonElement element)
        {
            JsonValueKind kind = element.ValueKind;

            switch (kind)
            {
                case JsonValueKind.False:
                case JsonValueKind.True:
                case JsonValueKind.String:
                case JsonValueKind.Number:
                {
                    buf.Append(element.ToString());
                    buf.Append(", ");
                    break;
                }
                case JsonValueKind.Object:
                {
                    foreach (JsonProperty prop in element.EnumerateObject())
                    {
                        buf.Append(prop.Name);
                        buf.Append(", ");
                        DepthFirstAppend(buf, prop.Value);
                    }

                    break;
                }
                case JsonValueKind.Array:
                {
                    foreach (JsonElement child in element.EnumerateArray())
                    {
                        DepthFirstAppend(buf, child);
                    }

                    break;
                }
            }
        }

        [Theory]
        [InlineData("[{\"arrayWithObjects\":[\"text\",14,[],null,false,{},{\"time\":24},[\"1\",\"2\",\"3\"]]}]")]
        [InlineData("[{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]")]
        [InlineData("[{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}},{\"a\":{}}]")]
        [InlineData("{\"a\":\"b\"}")]
        [InlineData("{}")]
        [InlineData("[]")]
        public static void CustomParseJson(string jsonString)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                string actual = doc.PrintJson();

                TextReader reader = new StringReader(jsonString);
                string expected = JsonTestHelper.NewtonsoftReturnStringHelper(reader);

                Assert.Equal(expected, actual);
            }
        }

        [Fact]
        public static void ParseArray()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.SimpleArrayJson))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(2, root.GetArrayLength());

                string phoneNumber = root[0].GetString();
                int age = root[1].GetInt32();

                Assert.Equal("425-214-3151", phoneNumber);
                Assert.Equal(25, age);

                Assert.Throws<IndexOutOfRangeException>(() => root[2]);
            }
        }

        [Fact]
        public static void ParseSimpleObject()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.SimpleObjectJson))
            {
                JsonElement parsedObject = doc.RootElement;

                int age = parsedObject.GetProperty("age").GetInt32();
                string ageString = parsedObject.GetProperty("age").ToString();
                string first = parsedObject.GetProperty("first").GetString();
                string last = parsedObject.GetProperty("last").GetString();
                string phoneNumber = parsedObject.GetProperty("phoneNumber").GetString();
                string street = parsedObject.GetProperty("street").GetString();
                string city = parsedObject.GetProperty("city").GetString();
                int zip = parsedObject.GetProperty("zip").GetInt32();

                Assert.Equal(7, parsedObject.GetPropertyCount());
                Assert.True(parsedObject.TryGetProperty("age", out JsonElement age2));
                Assert.Equal(30, age2.GetInt32());

                Assert.Equal(30, age);
                Assert.Equal("30", ageString);
                Assert.Equal("John", first);
                Assert.Equal("Smith", last);
                Assert.Equal("425-214-3151", phoneNumber);
                Assert.Equal("1 Microsoft Way", street);
                Assert.Equal("Redmond", city);
                Assert.Equal(98052, zip);
            }
        }

        [Fact]
        public static void ParseNestedJson()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.ParseJson))
            {
                JsonElement parsedObject = doc.RootElement;

                Assert.Equal(1, parsedObject.GetArrayLength());
                JsonElement person = parsedObject[0];
                Assert.Equal(5, person.GetPropertyCount());
                double age = person.GetProperty("age").GetDouble();
                string first = person.GetProperty("first").GetString();
                string last = person.GetProperty("last").GetString();
                JsonElement phoneNums = person.GetProperty("phoneNumbers");
                Assert.Equal(2, phoneNums.GetArrayLength());
                string phoneNum1 = phoneNums[0].GetString();
                string phoneNum2 = phoneNums[1].GetString();
                JsonElement address = person.GetProperty("address");
                string street = address.GetProperty("street").GetString();
                string city = address.GetProperty("city").GetString();
                double zipCode = address.GetProperty("zip").GetDouble();
                const string ThrowsAnyway = "throws-anyway";

                Assert.Equal(30, age);
                Assert.Equal("John", first);
                Assert.Equal("Smith", last);
                Assert.Equal("425-000-1212", phoneNum1);
                Assert.Equal("425-000-1213", phoneNum2);
                Assert.Equal("1 Microsoft Way", street);
                Assert.Equal("Redmond", city);
                Assert.Equal(98052, zipCode);

                Assert.Throws<InvalidOperationException>(() => person.GetArrayLength());
                Assert.Throws<IndexOutOfRangeException>(() => phoneNums[2]);
                Assert.Throws<InvalidOperationException>(() => phoneNums.GetProperty("2"));
                Assert.Throws<KeyNotFoundException>(() => address.GetProperty("2"));
                Assert.Throws<InvalidOperationException>(() => address.GetProperty("city").GetDouble());
                Assert.Throws<InvalidOperationException>(() => address.GetProperty("city").GetBoolean());
                Assert.Throws<InvalidOperationException>(() => address.GetProperty("zip").GetString());
                Assert.Throws<InvalidOperationException>(() => person.GetProperty("phoneNumbers").GetString());
                Assert.Throws<InvalidOperationException>(() => person.GetString());
                Assert.Throws<InvalidOperationException>(() => person.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => person.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => person.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
            }
        }

        [Fact]
        public static void ParseBoolean()
        {
            using (JsonDocument doc = JsonDocument.Parse("[true,false]"))
            {
                JsonElement parsedObject = doc.RootElement;
                bool first = parsedObject[0].GetBoolean();
                bool second = parsedObject[1].GetBoolean();
                Assert.True(first);
                Assert.False(second);
            }
        }

        [Fact]
        public static void JsonArrayToString()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.ParseJson))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Array, root.ValueKind);
                Assert.Equal(SR.ParseJson, root.ToString());
            }
        }

        [Fact]
        public static void JsonObjectToString()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.BasicJson))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Object, root.ValueKind);
                Assert.Equal(SR.BasicJson, root.ToString());
            }
        }

        [Fact]
        public static void MixedArrayIndexing()
        {
            // The root object is an array with "complex" children
            // root[0] is a number (simple single forward)
            // root[1] is an object which needs to account for the start entry, the children, and end.
            // root[2] is the target inner array
            // root[3] is a simple value past two complex values
            //
            // Within root[2] the array has only simple values, so it uses a different indexing algorithm.
            const string json = " [ 6, { \"hi\": \"mom\" }, [ \"425-214-3151\", 25 ], null ] ";

            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;
                JsonElement target = root[2];

                Assert.Equal(2, target.GetArrayLength());

                string phoneNumber = target[0].GetString();
                int age = target[1].GetInt32();

                Assert.Equal("425-214-3151", phoneNumber);
                Assert.Equal(25, age);
                Assert.Equal(JsonValueKind.Null, root[3].ValueKind);

                Assert.Throws<IndexOutOfRangeException>(() => root[4]);
            }
        }

        [Theory]
        [InlineData(0)]
        [InlineData(sbyte.MaxValue)]
        [InlineData(sbyte.MinValue)]
        public static void ReadNumber_1Byte(sbyte value)
        {
            double expectedDouble = value;
            float expectedFloat = value;
            decimal expectedDecimal = value;

            using (JsonDocument doc = JsonDocument.Parse("    " + value + "  "))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.True(root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(value, sbyteVal);

                Assert.True(root.TryGetInt16(out short shortVal));
                Assert.Equal(value, shortVal);

                Assert.True(root.TryGetInt32(out int intVal));
                Assert.Equal(value, intVal);

                Assert.True(root.TryGetInt64(out long longVal));
                Assert.Equal(value, longVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Equal(value, root.GetInt32());
                Assert.Equal(value, root.GetInt64());

                if (value >= 0)
                {
                    byte expectedByte = (byte)value;
                    ushort expectedUShort = (ushort)value;
                    uint expectedUInt = (uint)value;
                    ulong expectedULong = (ulong)value;

                    Assert.True(root.TryGetByte(out byte byteVal));
                    Assert.Equal(expectedByte, byteVal);

                    Assert.True(root.TryGetUInt16(out ushort ushortVal));
                    Assert.Equal(expectedUShort, ushortVal);

                    Assert.True(root.TryGetUInt32(out uint uintVal));
                    Assert.Equal(expectedUInt, uintVal);

                    Assert.True(root.TryGetUInt64(out ulong ulongVal));
                    Assert.Equal(expectedULong, ulongVal);

                    Assert.Equal(expectedUInt, root.GetUInt32());
                    Assert.Equal(expectedULong, root.GetUInt64());
                }
                else
                {
                    Assert.False(root.TryGetByte(out byte byteValue));
                    Assert.Equal(0, byteValue);

                    Assert.False(root.TryGetUInt16(out ushort ushortValue));
                    Assert.Equal(0, ushortValue);

                    Assert.False(root.TryGetUInt32(out uint uintValue));
                    Assert.Equal(0U, uintValue);

                    Assert.False(root.TryGetUInt64(out ulong ulongValue));
                    Assert.Equal(0UL, ulongValue);

                    Assert.Throws<FormatException>(() => root.GetUInt32());
                    Assert.Throws<FormatException>(() => root.GetUInt64());
                }

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.TryGetBytesFromBase64(out byte[] bytes));
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [InlineData(0)]
        [InlineData(short.MaxValue)]
        [InlineData(short.MinValue)]
        public static void ReadNumber_2Bytes(short value)
        {
            double expectedDouble = value;
            float expectedFloat = value;
            decimal expectedDecimal = value;

            using (JsonDocument doc = JsonDocument.Parse("    " + value + "  "))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.Equal((value == 0), root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.Equal((value == 0), root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.True(root.TryGetInt16(out short shortVal));
                Assert.Equal(value, shortVal);

                Assert.True(root.TryGetInt32(out int intVal));
                Assert.Equal(value, intVal);

                Assert.True(root.TryGetInt64(out long longVal));
                Assert.Equal(value, longVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Equal(value, root.GetInt32());
                Assert.Equal(value, root.GetInt64());

                if (value >= 0)
                {
                    byte expectedByte = (byte)value;
                    ushort expectedUShort = (ushort)value;
                    uint expectedUInt = (uint)value;
                    ulong expectedULong = (ulong)value;

                    Assert.True(root.TryGetUInt16(out ushort ushortVal));
                    Assert.Equal(expectedUShort, ushortVal);

                    Assert.True(root.TryGetUInt32(out uint uintVal));
                    Assert.Equal(expectedUInt, uintVal);

                    Assert.True(root.TryGetUInt64(out ulong ulongVal));
                    Assert.Equal(expectedULong, ulongVal);

                    Assert.Equal(expectedUInt, root.GetUInt32());
                    Assert.Equal(expectedULong, root.GetUInt64());
                }
                else
                {
                    Assert.False(root.TryGetUInt16(out ushort ushortValue));
                    Assert.Equal(0, ushortValue);

                    Assert.False(root.TryGetUInt32(out uint uintValue));
                    Assert.Equal(0U, uintValue);

                    Assert.False(root.TryGetUInt64(out ulong ulongValue));
                    Assert.Equal(0UL, ulongValue);

                    Assert.Throws<FormatException>(() => root.GetUInt32());
                    Assert.Throws<FormatException>(() => root.GetUInt64());
                }

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.TryGetBytesFromBase64(out byte[] bytes));
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [InlineData(0)]
        [InlineData(int.MaxValue)]
        [InlineData(int.MinValue)]
        public static void ReadSmallInteger(int value)
        {
            double expectedDouble = value;
            float expectedFloat = value;
            decimal expectedDecimal = value;

            using (JsonDocument doc = JsonDocument.Parse("    " + value + "  "))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.Equal((value == 0), root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.Equal((value == 0), root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.Equal((value == 0), root.TryGetInt16(out short shortVal));
                Assert.Equal(0, shortVal);

                Assert.Equal((value == 0), root.TryGetUInt16(out ushort ushortVal));
                Assert.Equal(0, ushortVal);

                Assert.True(root.TryGetInt32(out int intVal));
                Assert.Equal(value, intVal);

                Assert.True(root.TryGetInt64(out long longVal));
                Assert.Equal(value, longVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Equal(value, root.GetInt32());
                Assert.Equal(value, root.GetInt64());

                if (value >= 0)
                {
                    uint expectedUInt = (uint)value;
                    ulong expectedULong = (ulong)value;

                    Assert.True(root.TryGetUInt32(out uint uintVal));
                    Assert.Equal(expectedUInt, uintVal);

                    Assert.True(root.TryGetUInt64(out ulong ulongVal));
                    Assert.Equal(expectedULong, ulongVal);

                    Assert.Equal(expectedUInt, root.GetUInt32());
                    Assert.Equal(expectedULong, root.GetUInt64());
                }
                else
                {
                    Assert.False(root.TryGetUInt32(out uint uintValue));
                    Assert.Equal(0U, uintValue);

                    Assert.False(root.TryGetUInt64(out ulong ulongValue));
                    Assert.Equal(0UL, ulongValue);

                    Assert.Throws<FormatException>(() => root.GetUInt32());
                    Assert.Throws<FormatException>(() => root.GetUInt64());
                }

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.TryGetBytesFromBase64(out byte[] bytes));
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [InlineData((long)int.MaxValue + 1)]
        [InlineData((long)uint.MaxValue)]
        [InlineData(long.MaxValue)]
        [InlineData((long)int.MinValue - 1)]
        [InlineData(long.MinValue)]
        public static void ReadMediumInteger(long value)
        {
            double expectedDouble = value;
            float expectedFloat = value;
            decimal expectedDecimal = value;

            using (JsonDocument doc = JsonDocument.Parse("    " + value + "  "))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.False(root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.False(root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.False(root.TryGetInt16(out short shortVal));
                Assert.Equal(0, shortVal);

                Assert.False(root.TryGetUInt16(out ushort ushortVal));
                Assert.Equal(0, ushortVal);

                Assert.False(root.TryGetInt32(out int intVal));
                Assert.Equal(0, intVal);

                Assert.True(root.TryGetInt64(out long longVal));
                Assert.Equal(value, longVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Throws<FormatException>(() => root.GetInt32());
                Assert.Equal(value, root.GetInt64());

                if (value >= 0)
                {
                    if (value <= uint.MaxValue)
                    {
                        uint expectedUInt = (uint)value;
                        Assert.True(root.TryGetUInt32(out uint uintVal));
                        Assert.Equal(expectedUInt, uintVal);

                        Assert.Equal(expectedUInt, root.GetUInt64());
                    }
                    else
                    {
                        Assert.False(root.TryGetUInt32(out uint uintValue));
                        Assert.Equal(0U, uintValue);

                        Assert.Throws<FormatException>(() => root.GetUInt32());
                    }

                    ulong expectedULong = (ulong)value;
                    Assert.True(root.TryGetUInt64(out ulong ulongVal));
                    Assert.Equal(expectedULong, ulongVal);

                    Assert.Equal(expectedULong, root.GetUInt64());
                }
                else
                {
                    Assert.False(root.TryGetUInt32(out uint uintValue));
                    Assert.Equal(0U, uintValue);

                    Assert.False(root.TryGetUInt64(out ulong ulongValue));
                    Assert.Equal(0UL, ulongValue);

                    Assert.Throws<FormatException>(() => root.GetUInt32());
                    Assert.Throws<FormatException>(() => root.GetUInt64());
                }

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [InlineData((ulong)long.MaxValue + 1)]
        [InlineData(ulong.MaxValue)]
        public static void ReadLargeInteger(ulong value)
        {
            double expectedDouble = value;
            float expectedFloat = value;
            decimal expectedDecimal = value;

            using (JsonDocument doc = JsonDocument.Parse("    " + value + "  "))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.False(root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.False(root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.False(root.TryGetInt16(out short shortVal));
                Assert.Equal(0, shortVal);

                Assert.False(root.TryGetUInt16(out ushort ushortVal));
                Assert.Equal(0, ushortVal);

                Assert.False(root.TryGetInt32(out int intVal));
                Assert.Equal(0, intVal);

                Assert.False(root.TryGetUInt32(out uint uintVal));
                Assert.Equal(0U, uintVal);

                Assert.False(root.TryGetInt64(out long longVal));
                Assert.Equal(0L, longVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Throws<FormatException>(() => root.GetInt32());
                Assert.Throws<FormatException>(() => root.GetUInt32());
                Assert.Throws<FormatException>(() => root.GetInt64());

                Assert.True(root.TryGetUInt64(out ulong ulongVal));
                Assert.Equal(value, ulongVal);

                Assert.Equal(value, root.GetUInt64());

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Fact]
        public static void ReadTooLargeInteger()
        {
            float expectedFloat = ulong.MaxValue;
            double expectedDouble = ulong.MaxValue;
            decimal expectedDecimal = ulong.MaxValue;
            expectedDouble *= 10;
            expectedFloat *= 10;
            expectedDecimal *= 10;

            using (JsonDocument doc = JsonDocument.Parse("    " + ulong.MaxValue + "0  ", default))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.False(root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.False(root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.False(root.TryGetInt16(out short shortVal));
                Assert.Equal(0, shortVal);

                Assert.False(root.TryGetUInt16(out ushort ushortVal));
                Assert.Equal(0, ushortVal);

                Assert.False(root.TryGetInt32(out int intVal));
                Assert.Equal(0, intVal);

                Assert.False(root.TryGetUInt32(out uint uintVal));
                Assert.Equal(0U, uintVal);

                Assert.False(root.TryGetInt64(out long longVal));
                Assert.Equal(0L, longVal);

                Assert.False(root.TryGetUInt64(out ulong ulongVal));
                Assert.Equal(0UL, ulongVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Throws<FormatException>(() => root.GetInt32());
                Assert.Throws<FormatException>(() => root.GetUInt32());
                Assert.Throws<FormatException>(() => root.GetInt64());
                Assert.Throws<FormatException>(() => root.GetUInt64());

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [MemberData(nameof(JsonDateTimeTestData.ValidISO8601Tests), MemberType = typeof(JsonDateTimeTestData))]
        public static void ReadDateTimeAndDateTimeOffset(string jsonString, string expectedString)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                JsonElement root = doc.RootElement;

                DateTime expectedDateTime = DateTime.Parse(expectedString);
                DateTimeOffset expectedDateTimeOffset = DateTimeOffset.Parse(expectedString);

                Assert.Equal(JsonValueKind.String, root.ValueKind);

                Assert.True(root.TryGetDateTime(out DateTime DateTimeVal));
                Assert.Equal(expectedDateTime, DateTimeVal);

                Assert.True(root.TryGetDateTimeOffset(out DateTimeOffset DateTimeOffsetVal));
                Assert.Equal(expectedDateTimeOffset, DateTimeOffsetVal);

                Assert.Equal(expectedDateTime, root.GetDateTime());
                Assert.Equal(expectedDateTimeOffset, root.GetDateTimeOffset());

                Assert.Throws<InvalidOperationException>(() => root.GetSByte());
                Assert.Throws<InvalidOperationException>(() => root.GetByte());
                Assert.Throws<InvalidOperationException>(() => root.GetInt16());
                Assert.Throws<InvalidOperationException>(() => root.GetUInt16());
                Assert.Throws<InvalidOperationException>(() => root.GetInt32());
                Assert.Throws<InvalidOperationException>(() => root.GetUInt32());
                Assert.Throws<InvalidOperationException>(() => root.GetInt64());
                Assert.Throws<InvalidOperationException>(() => root.GetUInt64());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [MemberData(nameof(JsonDateTimeTestData.ValidISO8601TestsWithUtcOffset), MemberType = typeof(JsonDateTimeTestData))]
        public static void ReadDateTimeAndDateTimeOffset_WithUtcOffset(string jsonString, string expectedString)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                JsonElement root = doc.RootElement;

                DateTime expectedDateTime = DateTime.ParseExact(expectedString, "O", CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind);
                DateTimeOffset expectedDateTimeOffset = DateTimeOffset.ParseExact(expectedString, "O", CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind);

                Assert.Equal(JsonValueKind.String, root.ValueKind);

                Assert.True(root.TryGetDateTime(out DateTime DateTimeVal));
                Assert.Equal(expectedDateTime, DateTimeVal);

                Assert.True(root.TryGetDateTimeOffset(out DateTimeOffset DateTimeOffsetVal));
                Assert.Equal(expectedDateTimeOffset, DateTimeOffsetVal);

                Assert.Equal(expectedDateTime, root.GetDateTime());
                Assert.Equal(expectedDateTimeOffset, root.GetDateTimeOffset());
            }
        }

        [Theory]
        [MemberData(nameof(JsonDateTimeTestData.InvalidISO8601Tests), MemberType = typeof(JsonDateTimeTestData))]
        public static void ReadDateTimeAndDateTimeOffset_InvalidTests(string jsonString)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.String, root.ValueKind);

                Assert.False(root.TryGetDateTime(out DateTime DateTimeVal));
                Assert.Equal(default, DateTimeVal);
                Assert.False(root.TryGetDateTimeOffset(out DateTimeOffset DateTimeOffsetVal));
                Assert.Equal(default, DateTimeOffsetVal);

                Assert.Throws<FormatException>(() => root.GetDateTime());
                Assert.Throws<FormatException>(() => root.GetDateTimeOffset());
            }
        }

        [Theory]
        [MemberData(nameof(JsonGuidTestData.ValidGuidTests), MemberType = typeof(JsonGuidTestData))]
        [MemberData(nameof(JsonGuidTestData.ValidHexGuidTests), MemberType = typeof(JsonGuidTestData))]
        public static void ReadGuid(string jsonString, string expectedStr)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes($"\"{jsonString}\"");

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                JsonElement root = doc.RootElement;

                Guid expected = new Guid(expectedStr);

                Assert.Equal(JsonValueKind.String, root.ValueKind);

                Assert.True(root.TryGetGuid(out Guid GuidVal));
                Assert.Equal(expected, GuidVal);

                Assert.Equal(expected, root.GetGuid());

                Assert.Throws<InvalidOperationException>(() => root.GetSByte());
                Assert.Throws<InvalidOperationException>(() => root.GetByte());
                Assert.Throws<InvalidOperationException>(() => root.GetInt16());
                Assert.Throws<InvalidOperationException>(() => root.GetUInt16());
                Assert.Throws<InvalidOperationException>(() => root.GetInt32());
                Assert.Throws<InvalidOperationException>(() => root.GetUInt32());
                Assert.Throws<InvalidOperationException>(() => root.GetInt64());
                Assert.Throws<InvalidOperationException>(() => root.GetUInt64());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Theory]
        [MemberData(nameof(JsonGuidTestData.InvalidGuidTests), MemberType = typeof(JsonGuidTestData))]
        public static void ReadGuid_InvalidTests(string jsonString)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes($"\"{jsonString}\"");

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.String, root.ValueKind);

                Assert.False(root.TryGetGuid(out Guid GuidVal));
                Assert.Equal(default, GuidVal);

                Assert.Throws<FormatException>(() => root.GetGuid());
            }
        }

        public static IEnumerable<object[]> NonIntegerCases
        {
            get
            {
                yield return new object[] { "1e+1", 10.0, 10.0f, 10m };
                yield return new object[] { "1.1e-0", 1.1, 1.1f, 1.1m };
                yield return new object[] { "3.14159", 3.14159, 3.14159f, 3.14159m };
                yield return new object[] { "1e-10", 1e-10, 1e-10f, 1e-10m };
                yield return new object[] { "1234567.15", 1234567.15, 1234567.13f, 1234567.15m };
            }
        }

        [Theory]
        [MemberData(nameof(NonIntegerCases))]
        public static void ReadNonInteger(string str, double expectedDouble, float expectedFloat, decimal expectedDecimal)
        {
            using (JsonDocument doc = JsonDocument.Parse("    " + str + "  "))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                Assert.True(root.TryGetSingle(out float floatVal));
                Assert.Equal(expectedFloat, floatVal);

                Assert.True(root.TryGetDouble(out double doubleVal));
                Assert.Equal(expectedDouble, doubleVal);

                Assert.True(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(expectedDecimal, decimalVal);

                Assert.False(root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.False(root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.False(root.TryGetInt16(out short shortVal));
                Assert.Equal(0, shortVal);

                Assert.False(root.TryGetUInt16(out ushort ushortVal));
                Assert.Equal(0, ushortVal);

                Assert.False(root.TryGetInt32(out int intVal));
                Assert.Equal(0, intVal);

                Assert.False(root.TryGetUInt32(out uint uintVal));
                Assert.Equal(0U, uintVal);

                Assert.False(root.TryGetInt64(out long longVal));
                Assert.Equal(0L, longVal);

                Assert.False(root.TryGetUInt64(out ulong ulongVal));
                Assert.Equal(0UL, ulongVal);

                Assert.Equal(expectedFloat, root.GetSingle());
                Assert.Equal(expectedDouble, root.GetDouble());
                Assert.Equal(expectedDecimal, root.GetDecimal());
                Assert.Throws<FormatException>(() => root.GetSByte());
                Assert.Throws<FormatException>(() => root.GetByte());
                Assert.Throws<FormatException>(() => root.GetInt16());
                Assert.Throws<FormatException>(() => root.GetUInt16());
                Assert.Throws<FormatException>(() => root.GetInt32());
                Assert.Throws<FormatException>(() => root.GetUInt32());
                Assert.Throws<FormatException>(() => root.GetInt64());
                Assert.Throws<FormatException>(() => root.GetUInt64());

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Fact]
        public static void ReadTooPreciseDouble()
        {
            using (JsonDocument doc = JsonDocument.Parse("    1e+100000002"))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Number, root.ValueKind);

                if (PlatformDetection.IsNetFramework)
                {
                    Assert.False(root.TryGetSingle(out float floatVal));
                    Assert.Equal(0f, floatVal);

                    Assert.False(root.TryGetDouble(out double doubleVal));
                    Assert.Equal(0d, doubleVal);
                }
                else
                {
                    Assert.True(root.TryGetSingle(out float floatVal));
                    Assert.Equal(float.PositiveInfinity, floatVal);

                    Assert.True(root.TryGetDouble(out double doubleVal));
                    Assert.Equal(double.PositiveInfinity, doubleVal);
                }

                Assert.False(root.TryGetDecimal(out decimal decimalVal));
                Assert.Equal(0m, decimalVal);

                Assert.False(root.TryGetSByte(out sbyte sbyteVal));
                Assert.Equal(0, sbyteVal);

                Assert.False(root.TryGetByte(out byte byteVal));
                Assert.Equal(0, byteVal);

                Assert.False(root.TryGetInt16(out short shortVal));
                Assert.Equal(0, shortVal);

                Assert.False(root.TryGetUInt16(out ushort ushortVal));
                Assert.Equal(0, ushortVal);

                Assert.False(root.TryGetInt32(out int intVal));
                Assert.Equal(0, intVal);

                Assert.False(root.TryGetUInt32(out uint uintVal));
                Assert.Equal(0U, uintVal);

                Assert.False(root.TryGetInt64(out long longVal));
                Assert.Equal(0L, longVal);

                Assert.False(root.TryGetUInt64(out ulong ulongVal));
                Assert.Equal(0UL, ulongVal);

                if (PlatformDetection.IsNetFramework)
                {
                    Assert.Throws<FormatException>(() => root.GetSingle());
                    Assert.Throws<FormatException>(() => root.GetDouble());
                }
                else
                {
                    Assert.Equal(float.PositiveInfinity, root.GetSingle());
                    Assert.Equal(double.PositiveInfinity, root.GetDouble());
                }

                Assert.Throws<FormatException>(() => root.GetDecimal());
                Assert.Throws<FormatException>(() => root.GetSByte());
                Assert.Throws<FormatException>(() => root.GetByte());
                Assert.Throws<FormatException>(() => root.GetInt16());
                Assert.Throws<FormatException>(() => root.GetUInt16());
                Assert.Throws<FormatException>(() => root.GetInt32());
                Assert.Throws<FormatException>(() => root.GetUInt32());
                Assert.Throws<FormatException>(() => root.GetInt64());
                Assert.Throws<FormatException>(() => root.GetUInt64());

                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
                Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Fact]
        public static void ReadArrayWithComments()
        {
            var options = new JsonDocumentOptions
            {
                CommentHandling = JsonCommentHandling.Skip,
            };

            using (JsonDocument doc = JsonDocument.Parse(
                "[ 0, 1, 2, 3/*.14159*/           , /* 42, 11, hut, hut, hike! */ 4 ]",
                options))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.Array, root.ValueKind);
                Assert.Equal(5, root.GetArrayLength());

                for (int i = root.GetArrayLength() - 1; i >= 0; i--)
                {
                    Assert.Equal(i, root[i].GetInt32());
                }

                int val = 0;

                foreach (JsonElement element in root.EnumerateArray())
                {
                    Assert.Equal(val, element.GetInt32());
                    val++;
                }

                Assert.Throws<InvalidOperationException>(() => root.GetDouble());
                Assert.Throws<InvalidOperationException>(() => root.TryGetDouble(out double _));
                Assert.Throws<InvalidOperationException>(() => root.GetSByte());
                Assert.Throws<InvalidOperationException>(() => root.TryGetSByte(out sbyte _));
                Assert.Throws<InvalidOperationException>(() => root.GetByte());
                Assert.Throws<InvalidOperationException>(() => root.TryGetByte(out byte _));
                Assert.Throws<InvalidOperationException>(() => root.GetInt16());
                Assert.Throws<InvalidOperationException>(() => root.TryGetInt16(out short _));
                Assert.Throws<InvalidOperationException>(() => root.GetUInt16());
                Assert.Throws<InvalidOperationException>(() => root.TryGetUInt16(out ushort _));
                Assert.Throws<InvalidOperationException>(() => root.GetInt32());
                Assert.Throws<InvalidOperationException>(() => root.TryGetInt32(out int _));
                Assert.Throws<InvalidOperationException>(() => root.GetUInt32());
                Assert.Throws<InvalidOperationException>(() => root.TryGetUInt32(out uint _));
                Assert.Throws<InvalidOperationException>(() => root.GetInt64());
                Assert.Throws<InvalidOperationException>(() => root.TryGetInt64(out long _));
                Assert.Throws<InvalidOperationException>(() => root.GetUInt64());
                Assert.Throws<InvalidOperationException>(() => root.TryGetUInt64(out ulong _));
                Assert.Throws<InvalidOperationException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
                Assert.Throws<InvalidOperationException>(() => root.TryGetBytesFromBase64(out byte[] _));
                Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
                Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
                Assert.Throws<InvalidOperationException>(() => root.GetGuid());
                Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
                Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            }
        }

        [Fact]
        public static void CheckUseAfterDispose()
        {
            var buffer = new ArrayBufferWriter<byte>(1024);
            using (JsonDocument doc = JsonDocument.Parse("{\"First\":1}", default))
            {
                JsonElement root = doc.RootElement;
                JsonProperty property = root.EnumerateObject().First();
                doc.Dispose();

                Assert.Throws<ObjectDisposedException>(() => root.ValueKind);
                Assert.Throws<ObjectDisposedException>(() => root.GetArrayLength());
                Assert.Throws<ObjectDisposedException>(() => root.GetPropertyCount());
                Assert.Throws<ObjectDisposedException>(() => root.EnumerateArray());
                Assert.Throws<ObjectDisposedException>(() => root.EnumerateObject());
                Assert.Throws<ObjectDisposedException>(() => root.GetDouble());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetDouble(out double _));
                Assert.Throws<ObjectDisposedException>(() => root.GetSByte());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetSByte(out sbyte _));
                Assert.Throws<ObjectDisposedException>(() => root.GetByte());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetByte(out byte _));
                Assert.Throws<ObjectDisposedException>(() => root.GetInt16());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetInt16(out short _));
                Assert.Throws<ObjectDisposedException>(() => root.GetUInt16());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetUInt16(out ushort _));
                Assert.Throws<ObjectDisposedException>(() => root.GetInt32());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetInt32(out int _));
                Assert.Throws<ObjectDisposedException>(() => root.GetUInt32());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetUInt32(out uint _));
                Assert.Throws<ObjectDisposedException>(() => root.GetInt64());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetInt64(out long _));
                Assert.Throws<ObjectDisposedException>(() => root.GetUInt64());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetUInt64(out ulong _));
                Assert.Throws<ObjectDisposedException>(() => root.GetString());
                const string ThrowsAnyway = "throws-anyway";
                Assert.Throws<ObjectDisposedException>(() => root.ValueEquals(ThrowsAnyway));
                Assert.Throws<ObjectDisposedException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
                Assert.Throws<ObjectDisposedException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
                Assert.Throws<ObjectDisposedException>(() => root.GetBytesFromBase64());
                Assert.Throws<ObjectDisposedException>(() => root.TryGetBytesFromBase64(out byte[] _));
                Assert.Throws<ObjectDisposedException>(() => root.GetBoolean());
                Assert.Throws<ObjectDisposedException>(() => root.GetRawText());

                Assert.Throws<ObjectDisposedException>(() =>
                {
                    using var writer = new Utf8JsonWriter(buffer);
                    root.WriteTo(writer);
                });

                Assert.Throws<ObjectDisposedException>(() =>
                {
                    using var writer = new Utf8JsonWriter(buffer);
                    doc.WriteTo(writer);
                });

                Assert.Throws<ObjectDisposedException>(() =>
                {
                    using var writer = new Utf8JsonWriter(buffer);
                    property.WriteTo(writer);
                });
            }
        }

        [Fact]
        public static void CheckUseDefault()
        {
            JsonElement root = default;

            Assert.Equal(JsonValueKind.Undefined, root.ValueKind);

            Assert.Throws<InvalidOperationException>(() => root.GetArrayLength());
            Assert.Throws<InvalidOperationException>(() => root.GetPropertyCount());
            Assert.Throws<InvalidOperationException>(() => root.EnumerateArray());
            Assert.Throws<InvalidOperationException>(() => root.EnumerateObject());
            Assert.Throws<InvalidOperationException>(() => root.GetDouble());
            Assert.Throws<InvalidOperationException>(() => root.TryGetDouble(out double _));
            Assert.Throws<InvalidOperationException>(() => root.GetSByte());
            Assert.Throws<InvalidOperationException>(() => root.TryGetSByte(out sbyte _));
            Assert.Throws<InvalidOperationException>(() => root.GetByte());
            Assert.Throws<InvalidOperationException>(() => root.TryGetByte(out byte _));
            Assert.Throws<InvalidOperationException>(() => root.GetInt16());
            Assert.Throws<InvalidOperationException>(() => root.TryGetInt16(out short _));
            Assert.Throws<InvalidOperationException>(() => root.GetUInt16());
            Assert.Throws<InvalidOperationException>(() => root.TryGetUInt16(out ushort _));
            Assert.Throws<InvalidOperationException>(() => root.GetInt32());
            Assert.Throws<InvalidOperationException>(() => root.TryGetInt32(out int _));
            Assert.Throws<InvalidOperationException>(() => root.GetUInt32());
            Assert.Throws<InvalidOperationException>(() => root.TryGetUInt32(out uint _));
            Assert.Throws<InvalidOperationException>(() => root.GetInt64());
            Assert.Throws<InvalidOperationException>(() => root.TryGetInt64(out long _));
            Assert.Throws<InvalidOperationException>(() => root.GetUInt64());
            Assert.Throws<InvalidOperationException>(() => root.TryGetUInt64(out ulong _));
            Assert.Throws<InvalidOperationException>(() => root.GetString());
            const string ThrowsAnyway = "throws-anyway";
            Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway));
            Assert.Throws<InvalidOperationException>(() => root.ValueEquals(ThrowsAnyway.AsSpan()));
            Assert.Throws<InvalidOperationException>(() => root.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)));
            Assert.Throws<InvalidOperationException>(() => root.GetBytesFromBase64());
            Assert.Throws<InvalidOperationException>(() => root.TryGetBytesFromBase64(out byte[] _));
            Assert.Throws<InvalidOperationException>(() => root.GetDateTime());
            Assert.Throws<InvalidOperationException>(() => root.GetDateTimeOffset());
            Assert.Throws<InvalidOperationException>(() => root.GetGuid());
            Assert.Throws<InvalidOperationException>(() => root.GetBoolean());
            Assert.Throws<InvalidOperationException>(() => root.GetRawText());

            Assert.Throws<InvalidOperationException>(() =>
            {
                var buffer = new ArrayBufferWriter<byte>(1024);
                using var writer = new Utf8JsonWriter(buffer);
                root.WriteTo(writer);
            });
        }

        [Fact]
        public static void CheckInvalidString()
        {
            Assert.Throws<ArgumentException>(() => JsonDocument.Parse("{ \"unpaired\uDFFE\": true }"));
        }

        [Theory]
        [InlineData("\"hello\"    ", "hello")]
        [InlineData("    null     ", (string)null)]
        [InlineData("\"\\u0033\\u0031\"", "31")]
        public static void ReadString(string json, string expectedValue)
        {
            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                Assert.Equal(expectedValue, doc.RootElement.GetString());
            }
        }

        [Fact]
        public static void GetString_BadUtf8()
        {
            // The Arabic ligature Lam-Alef (U+FEFB) (which happens to, as a standalone, mean "no" in English)
            // is UTF-8 EF BB BB.  So let's leave out a BB and put it in quotes.
            byte[] badUtf8 = new byte[] { 0x22, 0xEF, 0xBB, 0x22 };
            using (JsonDocument doc = JsonDocument.Parse(badUtf8))
            {
                JsonElement root = doc.RootElement;

                const string ErrorMessage = "Cannot transcode invalid UTF-8 JSON text to UTF-16 string.";
                Assert.Equal(JsonValueKind.String, root.ValueKind);
                AssertExtensions.Throws<InvalidOperationException>(() => root.GetString(), ErrorMessage);
                Assert.Throws<InvalidOperationException>(() => root.GetRawText());
                const string DummyString = "dummy-string";
                Assert.False(root.ValueEquals(badUtf8));
                Assert.False(root.ValueEquals(DummyString));
                Assert.False(root.ValueEquals(DummyString.AsSpan()));
                Assert.False(root.ValueEquals(Encoding.UTF8.GetBytes(DummyString)));
            }
        }

        [Fact]
        public static void GetBase64String_BadUtf8()
        {
            // The Arabic ligature Lam-Alef (U+FEFB) (which happens to, as a standalone, mean "no" in English)
            // is UTF-8 EF BB BB.  So let's leave out a BB and put it in quotes.
            using (JsonDocument doc = JsonDocument.Parse(new byte[] { 0x22, 0xEF, 0xBB, 0x22 }))
            {
                JsonElement root = doc.RootElement;

                Assert.Equal(JsonValueKind.String, root.ValueKind);
                Assert.Throws<FormatException>(() => root.GetBytesFromBase64());
                Assert.False(root.TryGetBytesFromBase64(out byte[] value));
                Assert.Null(value);
            }
        }

        [Fact]
        public static void GetBase64Unescapes()
        {
            string jsonString = "\"\\u0031234\""; // equivalent to "\"1234\""

            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8))
            {
                byte[] expected = Convert.FromBase64String("1234"); // new byte[3] { 215, 109, 248 }

                Assert.Equal(expected, doc.RootElement.GetBytesFromBase64());

                Assert.True(doc.RootElement.TryGetBytesFromBase64(out byte[] value));
                Assert.Equal(expected, value);
            }
        }

        [Theory]
        [MemberData(nameof(JsonBase64TestData.ValidBase64Tests), MemberType = typeof(JsonBase64TestData))]
        public static void ReadBase64String(string jsonString)
        {
            byte[] expected = Convert.FromBase64String(jsonString.AsSpan(1, jsonString.Length - 2).ToString());

            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                Assert.Equal(expected, doc.RootElement.GetBytesFromBase64());

                Assert.True(doc.RootElement.TryGetBytesFromBase64(out byte[] value));
                Assert.Equal(expected, value);
            }
        }

        [Theory]
        [MemberData(nameof(JsonBase64TestData.InvalidBase64Tests), MemberType = typeof(JsonBase64TestData))]
        public static void InvalidBase64(string jsonString)
        {
            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8))
            {
                Assert.False(doc.RootElement.TryGetBytesFromBase64(out byte[] value));
                Assert.Null(value);

                Assert.Throws<FormatException>(() => doc.RootElement.GetBytesFromBase64());
            }
        }

        [Theory]
        [InlineData(" { \"hi\": \"there\" }")]
        [InlineData(" { \n\n\n\n } ")]
        [InlineData(" { \"outer\": { \"inner\": [ 1, 2, 3 ] }, \"secondOuter\": [ 2, 4, 6, 0, 1 ] }")]
        public static void TryGetProperty_NoProperty(string json)
        {
            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;

                const string NotPresent = "Not Present";
                byte[] notPresentUtf8 = Encoding.UTF8.GetBytes(NotPresent);

                JsonElement element;
                Assert.False(root.TryGetProperty(NotPresent, out element));
                Assert.Equal(default, element);
                Assert.False(root.TryGetProperty(NotPresent.AsSpan(), out element));
                Assert.Equal(default, element);
                Assert.False(root.TryGetProperty(notPresentUtf8, out element));
                Assert.Equal(default, element);
                Assert.False(root.TryGetProperty(new string('z', 512), out element));
                Assert.Equal(default, element);

                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(NotPresent));
                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(NotPresent.AsSpan()));
                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(notPresentUtf8));
                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(new string('z', 512)));
            }
        }

        [Fact]
        public static void TryGetProperty_CaseSensitive()
        {
            const string PascalString = "Needle";
            const string CamelString = "needle";
            const string OddString = "nEeDle";
            const string InverseOddString = "NeEdLE";

            string json = $"{{ \"{PascalString}\": \"no\", \"{CamelString}\": 42, \"{OddString}\": false }}";

            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;

                byte[] pascalBytes = Encoding.UTF8.GetBytes(PascalString);
                byte[] camelBytes = Encoding.UTF8.GetBytes(CamelString);
                byte[] oddBytes = Encoding.UTF8.GetBytes(OddString);
                byte[] inverseOddBytes = Encoding.UTF8.GetBytes(InverseOddString);

                void assertPascal(JsonElement elem)
                {
                    Assert.Equal(JsonValueKind.String, elem.ValueKind);
                    Assert.Equal("no", elem.GetString());
                }

                void assertCamel(JsonElement elem)
                {
                    Assert.Equal(JsonValueKind.Number, elem.ValueKind);
                    Assert.Equal(42, elem.GetInt32());
                }

                void assertOdd(JsonElement elem)
                {
                    Assert.Equal(JsonValueKind.False, elem.ValueKind);
                    Assert.False(elem.GetBoolean());
                }

                Assert.True(root.TryGetProperty(PascalString, out JsonElement pascal));
                assertPascal(pascal);
                Assert.True(root.TryGetProperty(PascalString.AsSpan(), out pascal));
                assertPascal(pascal);
                Assert.True(root.TryGetProperty(pascalBytes, out pascal));
                assertPascal(pascal);

                Assert.True(root.TryGetProperty(CamelString, out JsonElement camel));
                assertCamel(camel);
                Assert.True(root.TryGetProperty(CamelString.AsSpan(), out camel));
                assertCamel(camel);
                Assert.True(root.TryGetProperty(camelBytes, out camel));
                assertCamel(camel);

                Assert.True(root.TryGetProperty(OddString, out JsonElement odd));
                assertOdd(odd);
                Assert.True(root.TryGetProperty(OddString.AsSpan(), out odd));
                assertOdd(odd);
                Assert.True(root.TryGetProperty(oddBytes, out odd));
                assertOdd(odd);

                Assert.False(root.TryGetProperty(InverseOddString, out _));
                Assert.False(root.TryGetProperty(InverseOddString.AsSpan(), out _));
                Assert.False(root.TryGetProperty(inverseOddBytes, out _));

                assertPascal(root.GetProperty(PascalString));
                assertPascal(root.GetProperty(PascalString.AsSpan()));
                assertPascal(root.GetProperty(pascalBytes));

                assertCamel(root.GetProperty(CamelString));
                assertCamel(root.GetProperty(CamelString.AsSpan()));
                assertCamel(root.GetProperty(camelBytes));

                assertOdd(root.GetProperty(OddString));
                assertOdd(root.GetProperty(OddString.AsSpan()));
                assertOdd(root.GetProperty(oddBytes));

                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(InverseOddString));
                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(InverseOddString.AsSpan()));
                Assert.Throws<KeyNotFoundException>(() => root.GetProperty(inverseOddBytes));
            }
        }

        [Theory]
        [InlineData(">>")]
        [InlineData(">>>")]
        [InlineData(">>a")]
        [InlineData(">a>")]
        public static void TryGetDateTimeAndOffset_InvalidPropertyValue(string testData)
        {
            string jsonString = JsonSerializer.Serialize(new { DateTimeProperty = testData });

            byte[] dataUtf8 = Encoding.UTF8.GetBytes(jsonString);

            using (JsonDocument doc = JsonDocument.Parse(dataUtf8, default))
            {
                JsonElement root = doc.RootElement;

                Assert.False(root.GetProperty("DateTimeProperty").TryGetDateTime(out DateTime datetimeValue));
                Assert.Equal(default, datetimeValue);
                Assert.Throws<FormatException>(() => root.GetProperty("DateTimeProperty").GetDateTime());

                Assert.False(root.GetProperty("DateTimeProperty").TryGetDateTimeOffset(out DateTimeOffset datetimeOffsetValue));
                Assert.Equal(default, datetimeOffsetValue);
                Assert.Throws<FormatException>(() => root.GetProperty("DateTimeProperty").GetDateTimeOffset());
            }
        }

        [Theory]
        [InlineData("")]
        [InlineData("    ")]
        [InlineData("1 2")]
        [InlineData("[ 1")]
        public static Task CheckUnparsable(string json)
        {
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(json));

            byte[] utf8 = Encoding.UTF8.GetBytes(json);
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(utf8));

            ReadOnlySequence<byte> singleSeq = new ReadOnlySequence<byte>(utf8);
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(singleSeq));

            ReadOnlySequence<byte> multiSegment = JsonTestHelper.SegmentInto(utf8, 6);
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(multiSegment));

            Stream stream = new MemoryStream(utf8);
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(stream));

            stream.Seek(0, SeekOrigin.Begin);
            return Assert.ThrowsAnyAsync<JsonException>(() => JsonDocument.ParseAsync(stream));
        }

        [Fact]
        public static void CheckParseDepth()
        {
            const int OkayCount = 64;
            string okayJson = new string('[', OkayCount) + "2" + new string(']', OkayCount);
            int depth = 0;

            using (JsonDocument doc = JsonDocument.Parse(okayJson))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Array, root.ValueKind);

                JsonElement cur = root;

                while (cur.ValueKind == JsonValueKind.Array)
                {
                    Assert.Equal(1, cur.GetArrayLength());
                    cur = cur[0];
                    depth++;
                }

                Assert.Equal(JsonValueKind.Number, cur.ValueKind);
                Assert.Equal(2, cur.GetInt32());
                Assert.Equal(OkayCount, depth);
            }

            string badJson = $"[{okayJson}]";

            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(badJson));
        }

        [Fact]
        public static void HonorReaderOptionsMaxDepth()
        {
            const int OkayCount = 65;
            string okayJson = new string('[', OkayCount) + "2" + new string(']', OkayCount);
            int depth = 0;

            using (JsonDocument doc = JsonDocument.Parse(okayJson, new JsonDocumentOptions { MaxDepth = OkayCount }))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Array, root.ValueKind);

                JsonElement cur = root;

                while (cur.ValueKind == JsonValueKind.Array)
                {
                    Assert.Equal(1, cur.GetArrayLength());
                    cur = cur[0];
                    depth++;
                }

                Assert.Equal(JsonValueKind.Number, cur.ValueKind);
                Assert.Equal(2, cur.GetInt32());
                Assert.Equal(OkayCount, depth);
            }

            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(okayJson, new JsonDocumentOptions { MaxDepth = 32 }));
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(okayJson));
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(okayJson, new JsonDocumentOptions { MaxDepth = 0 }));
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(okayJson, new JsonDocumentOptions { MaxDepth = 64 }));
        }

        [Fact]
        public static void LargeMaxDepthIsAllowed()
        {
            // MaxDepthOverflow * 8 > int.MaxValue
            const int MaxDepthOverflow = 1 << 28; //268_435_456;

            string okayJson = "[]";

            using (JsonDocument doc = JsonDocument.Parse(okayJson, new JsonDocumentOptions { MaxDepth = MaxDepthOverflow }))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Array, root.ValueKind);
            }

            using (JsonDocument doc = JsonDocument.Parse(okayJson, new JsonDocumentOptions { MaxDepth = int.MaxValue }))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Array, root.ValueKind);
            }
        }

        [Fact]
        public static void EnableComments()
        {
            string json = "3";
            byte[] utf8 = Encoding.UTF8.GetBytes(json);

            var readerOptions = new JsonReaderOptions
            {
                CommentHandling = JsonCommentHandling.Allow,
            };

            AssertExtensions.Throws<ArgumentException>(
                "reader",
                () =>
                {
                    JsonReaderState state = new JsonReaderState(readerOptions);
                    Utf8JsonReader reader = new Utf8JsonReader(utf8, isFinalBlock: false, state);
                    JsonDocument.ParseValue(ref reader);
                });

            AssertExtensions.Throws<ArgumentException>(
                "reader",
                () =>
                {
                    JsonReaderState state = new JsonReaderState(readerOptions);
                    Utf8JsonReader reader = new Utf8JsonReader(utf8, isFinalBlock: false, state);
                    JsonDocument.TryParseValue(ref reader, out _);
                });
        }

        [Theory]
        [InlineData(-1)]
        [InlineData((int)JsonCommentHandling.Allow)]
        [InlineData(3)]
        [InlineData(byte.MaxValue)]
        [InlineData(byte.MaxValue + 3)] // Other values, like byte.MaxValue + 1 overflows to 0 (i.e. JsonCommentHandling.Disallow), which is valid.
        [InlineData(byte.MaxValue + 4)]
        public static void ReadCommentHandlingDoesNotSupportAllow(int enumValue)
        {
            Assert.Throws<ArgumentOutOfRangeException>("value", () => new JsonDocumentOptions
            {
                CommentHandling = (JsonCommentHandling)enumValue
            });
        }

        [Fact]
        public static void ReadCommentHandlingWorksForNegativeOverflow()
        {
            var options = new JsonDocumentOptions
            {
                CommentHandling = unchecked((JsonCommentHandling)(-255))
            };
            Assert.Equal(JsonCommentHandling.Skip, options.CommentHandling);

            using (JsonDocument doc = JsonDocument.Parse("/* some comment */{ }", options))
            {
                Assert.Equal(JsonValueKind.Object, doc.RootElement.ValueKind);
            }
        }

        [Theory]
        [InlineData(-1)]
        public static void TestDepthInvalid(int depth)
        {
            Assert.Throws<ArgumentOutOfRangeException>("value", () => new JsonDocumentOptions
            {
                MaxDepth = depth
            });
        }

        [Theory]
        [InlineData("{ \"object\": { \"1-1\": null, \"1-2\": \"12\", }, \"array\": [ 4, 8, 1, 9, 2 ] }")]
        [InlineData("[ 5, 4, 3, 2, 1, ]")]
        [InlineData("{ \"shape\": \"square\", \"size\": 10, \"color\": \"green\", }")]
        public static void TrailingCommas(string json)
        {
            var options = new JsonDocumentOptions
            {
                AllowTrailingCommas = true
            };

            using (JsonDocument doc = JsonDocument.Parse(json, options))
            {
                Assert.Equal(json, doc.RootElement.GetRawText());
            }
            Assert.ThrowsAny<JsonException>(() => JsonDocument.Parse(json));
        }

        [Fact]
        public static void GetPropertyByNullName()
        {
            using (JsonDocument doc = JsonDocument.Parse("{ }"))
            {
                AssertExtensions.Throws<ArgumentNullException>(
                    "propertyName",
                    () => doc.RootElement.GetProperty((string)null));

                AssertExtensions.Throws<ArgumentNullException>(
                    "propertyName",
                    () => doc.RootElement.TryGetProperty((string)null, out _));
            }
        }

        [Fact]
        public static void GetPropertyInvalidUtf16()
        {
            using (JsonDocument doc = JsonDocument.Parse("{\"name\":\"value\"}"))
            {
                Assert.Throws<ArgumentException>(() => doc.RootElement.GetProperty("unpaired\uDFFE"));

                Assert.Throws<ArgumentException>(() => doc.RootElement.TryGetProperty("unpaired\uDFFE", out _));
            }
        }

        [Theory]
        [InlineData("short")]
        [InlineData("thisValueIsLongerThan86CharsSoWeDeferTheTranscodingUntilWeFindAViableCandidateAsAPropertyMatch")]
        public static void GetPropertyFindsLast(string propertyName)
        {
            string json = $"{{ \"{propertyName}\": 1, \"{propertyName}\": 2, \"nope\": -1, \"{propertyName}\": 3 }}";

            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;
                byte[] utf8PropertyName = Encoding.UTF8.GetBytes(propertyName);

                Assert.Equal(3, root.GetProperty(propertyName).GetInt32());
                Assert.Equal(3, root.GetProperty(propertyName.AsSpan()).GetInt32());
                Assert.Equal(3, root.GetProperty(utf8PropertyName).GetInt32());

                JsonElement matchedProperty;
                Assert.True(root.TryGetProperty(propertyName, out matchedProperty));
                Assert.Equal(3, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(propertyName.AsSpan(), out matchedProperty));
                Assert.Equal(3, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(utf8PropertyName, out matchedProperty));
                Assert.Equal(3, matchedProperty.GetInt32());
            }
        }

        [Theory]
        [InlineData("short")]
        [InlineData("thisValueIsLongerThan86CharsSoWeDeferTheTranscodingUntilWeFindAViableCandidateAsAPropertyMatch")]
        public static void GetPropertyFindsLast_WithEscaping(string propertyName)
        {
            string first = $"\\u{(int)propertyName[0]:X4}{propertyName.Substring(1)}";
            StringBuilder builder = new StringBuilder(propertyName.Length * 6);

            int half = propertyName.Length / 2;
            builder.Append(propertyName.Substring(0, half));

            for (int i = half; i < propertyName.Length; i++)
            {
                builder.AppendFormat("\\u{0:X4}", (int)propertyName[i]);
            }

            builder.Append('2');
            string second = builder.ToString();
            builder.Clear();

            for (int i = 0; i < propertyName.Length; i++)
            {
                if ((i & 1) == 0)
                {
                    builder.Append(propertyName[i]);
                }
                else
                {
                    builder.AppendFormat("\\u{0:X4}", (int)propertyName[i]);
                }
            }

            builder.Append('3');
            string third = builder.ToString();

            string pn2 = propertyName + "2";
            string pn3 = propertyName + "3";

            string json =
                $"{{ \"{propertyName}\": 0, \"{first}\": 1, \"{pn2}\": 0, \"{second}\": 2, \"{pn3}\": 0, \"nope\": -1, \"{third}\": 3 }}";

            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;
                byte[] utf8PropertyName = Encoding.UTF8.GetBytes(propertyName);
                byte[] utf8PropertyName2 = Encoding.UTF8.GetBytes(pn2);
                byte[] utf8PropertyName3 = Encoding.UTF8.GetBytes(pn3);

                Assert.Equal(1, root.GetProperty(propertyName).GetInt32());
                Assert.Equal(1, root.GetProperty(propertyName.AsSpan()).GetInt32());
                Assert.Equal(1, root.GetProperty(utf8PropertyName).GetInt32());

                Assert.Equal(2, root.GetProperty(pn2).GetInt32());
                Assert.Equal(2, root.GetProperty(pn2.AsSpan()).GetInt32());
                Assert.Equal(2, root.GetProperty(utf8PropertyName2).GetInt32());

                Assert.Equal(3, root.GetProperty(pn3).GetInt32());
                Assert.Equal(3, root.GetProperty(pn3.AsSpan()).GetInt32());
                Assert.Equal(3, root.GetProperty(utf8PropertyName3).GetInt32());

                JsonElement matchedProperty;
                Assert.True(root.TryGetProperty(propertyName, out matchedProperty));
                Assert.Equal(1, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(propertyName.AsSpan(), out matchedProperty));
                Assert.Equal(1, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(utf8PropertyName, out matchedProperty));
                Assert.Equal(1, matchedProperty.GetInt32());

                Assert.True(root.TryGetProperty(pn2, out matchedProperty));
                Assert.Equal(2, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(pn2.AsSpan(), out matchedProperty));
                Assert.Equal(2, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(utf8PropertyName2, out matchedProperty));
                Assert.Equal(2, matchedProperty.GetInt32());

                Assert.True(root.TryGetProperty(pn3, out matchedProperty));
                Assert.Equal(3, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(pn3.AsSpan(), out matchedProperty));
                Assert.Equal(3, matchedProperty.GetInt32());
                Assert.True(root.TryGetProperty(utf8PropertyName3, out matchedProperty));
                Assert.Equal(3, matchedProperty.GetInt32());
            }
        }

        [Fact]
        public static void GetRawText()
        {
            const string json =
// Don't let there be a newline before the first embedded quote,
// because the index would change across CRLF vs LF compile environments.
@"{  ""  weird  property  name""
                  :
       {
         ""nested"":
         [ 1, 2, 3
,
4, 5, 6 ],
        ""also"" : 3
  },
  ""number"": 1.02e+4,
  ""bool"": false,
  ""n\u0075ll"": null,
  ""multiLineArray"":

[

0,
1,
2,

    3

],
  ""string"":

""Aren't string just the greatest?\r\nNot a terminating quote: \""     \r   \n   \t  \\   ""
}";

            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                Assert.Equal(6, doc.RootElement.GetPropertyCount());
                JsonElement.ObjectEnumerator enumerator = doc.RootElement.EnumerateObject();
                Assert.True(enumerator.MoveNext(), "Move to first property");
                JsonProperty property = enumerator.Current;

                Assert.Equal("  weird  property  name", property.Name);
                string rawText = property.ToString();
                int crCount = rawText.Count(c => c == '\r');
                Assert.Equal(2, property.Value.GetPropertyCount());
                Assert.Equal(128 + crCount, rawText.Length);
                Assert.Equal('\"', rawText[0]);
                Assert.Equal(' ', rawText[1]);
                Assert.Equal('}', rawText[rawText.Length - 1]);
                Assert.Equal(json.Substring(json.IndexOf('\"'), rawText.Length), rawText);

                Assert.True(enumerator.MoveNext(), "Move to number property");
                property = enumerator.Current;

                Assert.Equal("number", property.Name);
                Assert.Equal("\"number\": 1.02e+4", property.ToString());
                Assert.Equal(10200.0, property.Value.GetDouble());
                Assert.Equal("1.02e+4", property.Value.GetRawText());

                Assert.True(enumerator.MoveNext(), "Move to bool property");
                property = enumerator.Current;

                Assert.Equal("bool", property.Name);
                Assert.False(property.Value.GetBoolean());
                Assert.Equal("false", property.Value.GetRawText());
                Assert.Equal(bool.FalseString, property.Value.ToString());

                Assert.True(enumerator.MoveNext(), "Move to null property");
                property = enumerator.Current;

                Assert.Equal("null", property.Name);
                Assert.Equal("null", property.Value.GetRawText());
                Assert.Equal(string.Empty, property.Value.ToString());
                Assert.Equal("\"n\\u0075ll\": null", property.ToString());

                Assert.True(enumerator.MoveNext(), "Move to multiLineArray property");
                property = enumerator.Current;

                Assert.Equal("multiLineArray", property.Name);
                Assert.Equal(4, property.Value.GetArrayLength());
                rawText = property.Value.GetRawText();
                Assert.Equal('[', rawText[0]);
                Assert.Equal(']', rawText[rawText.Length - 1]);
                Assert.Contains('3', rawText);
                Assert.Contains('\n', rawText);

                Assert.True(enumerator.MoveNext(), "Move to string property");
                property = enumerator.Current;

                Assert.Equal("string", property.Name);
                rawText = property.Value.GetRawText();
                Assert.Equal('\"', rawText[0]);
                Assert.Equal('\"', rawText[rawText.Length - 1]);
                string strValue = property.Value.GetString();
                int newlineIdx = strValue.IndexOf('\r');
                int colonIdx = strValue.IndexOf(':');
                int escapedQuoteIdx = colonIdx + 2;
                Assert.Equal(rawText.Substring(1, newlineIdx), strValue.Substring(0, newlineIdx));
                Assert.Equal('\\', rawText[escapedQuoteIdx + 3]);
                Assert.Equal('\"', rawText[escapedQuoteIdx + 4]);
                Assert.Equal('\"', strValue[escapedQuoteIdx]);
                Assert.Contains("\r", strValue);
                Assert.Contains(@"\r", rawText);
                string valueText = rawText;
                rawText = property.ToString();
                Assert.Equal('\"', rawText[0]);
                Assert.Equal('\"', rawText[rawText.Length - 1]);
                Assert.NotEqual(valueText, rawText);
                Assert.EndsWith(valueText, rawText);

                Assert.False(enumerator.MoveNext(), "Move past the last property");
            }
        }

        [Fact]
        public static void ArrayEnumeratorIndependentWalk()
        {
            using (JsonDocument doc = JsonDocument.Parse("[0, 1, 2, 3, 4, 5]"))
            {
                JsonElement root = doc.RootElement;
                JsonElement.ArrayEnumerator structEnumerable = root.EnumerateArray();
                IEnumerable<JsonElement> strongBoxedEnumerable = root.EnumerateArray();
                IEnumerable weakBoxedEnumerable = root.EnumerateArray();

                JsonElement.ArrayEnumerator structEnumerator = structEnumerable.GetEnumerator();
                IEnumerator<JsonElement> strongBoxedEnumerator = strongBoxedEnumerable.GetEnumerator();
                IEnumerator weakBoxedEnumerator = weakBoxedEnumerable.GetEnumerator();

                Assert.True(structEnumerator.MoveNext());
                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.True(weakBoxedEnumerator.MoveNext());

                Assert.Equal(0, structEnumerator.Current.GetInt32());
                Assert.Equal(0, strongBoxedEnumerator.Current.GetInt32());
                Assert.Equal(0, ((JsonElement)weakBoxedEnumerator.Current).GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.True(weakBoxedEnumerator.MoveNext());

                Assert.Equal(1, structEnumerator.Current.GetInt32());
                Assert.Equal(1, strongBoxedEnumerator.Current.GetInt32());
                Assert.Equal(1, ((JsonElement)weakBoxedEnumerator.Current).GetInt32());

                int test = 0;

                foreach (JsonElement element in structEnumerable)
                {
                    Assert.Equal(test, element.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonElement element in structEnumerable)
                {
                    Assert.Equal(test, element.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonElement element in strongBoxedEnumerable)
                {
                    Assert.Equal(test, element.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonElement element in strongBoxedEnumerable)
                {
                    Assert.Equal(test, element.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonElement element in weakBoxedEnumerable)
                {
                    Assert.Equal(test, element.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonElement element in weakBoxedEnumerable)
                {
                    Assert.Equal(test, element.GetInt32());
                    test++;
                }

                structEnumerator.Reset();

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal(0, structEnumerator.Current.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal(1, structEnumerator.Current.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal(2, structEnumerator.Current.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal(3, structEnumerator.Current.GetInt32());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal(2, strongBoxedEnumerator.Current.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal(4, structEnumerator.Current.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal(5, structEnumerator.Current.GetInt32());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal(3, strongBoxedEnumerator.Current.GetInt32());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal(2, ((JsonElement)weakBoxedEnumerator.Current).GetInt32());

                Assert.False(structEnumerator.MoveNext());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal(4, strongBoxedEnumerator.Current.GetInt32());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal(5, strongBoxedEnumerator.Current.GetInt32());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal(3, ((JsonElement)weakBoxedEnumerator.Current).GetInt32());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal(4, ((JsonElement)weakBoxedEnumerator.Current).GetInt32());

                Assert.False(structEnumerator.MoveNext());
                Assert.False(strongBoxedEnumerator.MoveNext());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal(5, ((JsonElement)weakBoxedEnumerator.Current).GetInt32());

                Assert.False(weakBoxedEnumerator.MoveNext());
                Assert.False(structEnumerator.MoveNext());
                Assert.False(strongBoxedEnumerator.MoveNext());
                Assert.False(weakBoxedEnumerator.MoveNext());
            }
        }

        [Fact]
        public static void DefaultArrayEnumeratorDoesNotThrow()
        {
            JsonElement.ArrayEnumerator enumerable = default;
            JsonElement.ArrayEnumerator enumerator = enumerable.GetEnumerator();
            JsonElement.ArrayEnumerator defaultEnumerator = default;

            Assert.Equal(JsonValueKind.Undefined, enumerable.Current.ValueKind);
            Assert.Equal(JsonValueKind.Undefined, enumerator.Current.ValueKind);

            Assert.False(enumerable.MoveNext());
            Assert.False(enumerable.MoveNext());
            Assert.False(defaultEnumerator.MoveNext());
        }

        [Fact]
        public static void ObjectEnumeratorIndependentWalk()
        {
            const string json = @"
{
  ""name0"": 0,
  ""name1"": 1,
  ""name2"": 2,
  ""name3"": 3,
  ""name4"": 4,
  ""name5"": 5
}";
            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;
                JsonElement.ObjectEnumerator structEnumerable = root.EnumerateObject();
                IEnumerable<JsonProperty> strongBoxedEnumerable = root.EnumerateObject();
                IEnumerable weakBoxedEnumerable = root.EnumerateObject();

                JsonElement.ObjectEnumerator structEnumerator = structEnumerable.GetEnumerator();
                IEnumerator<JsonProperty> strongBoxedEnumerator = strongBoxedEnumerable.GetEnumerator();
                IEnumerator weakBoxedEnumerator = weakBoxedEnumerable.GetEnumerator();

                Assert.True(structEnumerator.MoveNext());
                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.True(weakBoxedEnumerator.MoveNext());

                Assert.Equal("name0", structEnumerator.Current.Name);
                Assert.Equal(0, structEnumerator.Current.Value.GetInt32());
                Assert.Equal("name0", strongBoxedEnumerator.Current.Name);
                Assert.Equal(0, strongBoxedEnumerator.Current.Value.GetInt32());
                Assert.Equal("name0", ((JsonProperty)weakBoxedEnumerator.Current).Name);
                Assert.Equal(0, ((JsonProperty)weakBoxedEnumerator.Current).Value.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.True(weakBoxedEnumerator.MoveNext());

                Assert.Equal(1, structEnumerator.Current.Value.GetInt32());
                Assert.Equal(1, strongBoxedEnumerator.Current.Value.GetInt32());
                Assert.Equal(1, ((JsonProperty)weakBoxedEnumerator.Current).Value.GetInt32());

                int test = 0;

                foreach (JsonProperty property in structEnumerable)
                {
                    Assert.Equal("name" + test, property.Name);
                    Assert.Equal(test, property.Value.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonProperty property in structEnumerable)
                {
                    Assert.Equal("name" + test, property.Name);
                    Assert.Equal(test, property.Value.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonProperty property in strongBoxedEnumerable)
                {
                    Assert.Equal("name" + test, property.Name);
                    Assert.Equal(test, property.Value.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonProperty property in strongBoxedEnumerable)
                {
                    string propertyName = property.Name;
                    Assert.Equal("name" + test, property.Name);
                    Assert.Equal(test, property.Value.GetInt32());
                    test++;

                    // Subsequent read of the same JsonProperty should return an equal string
                    string propertyName2 = property.Name;
                    Assert.Equal(propertyName, propertyName2);
                }

                test = 0;

                foreach (JsonProperty property in weakBoxedEnumerable)
                {
                    Assert.Equal("name" + test, property.Name);
                    Assert.Equal(test, property.Value.GetInt32());
                    test++;
                }

                test = 0;

                foreach (JsonProperty property in weakBoxedEnumerable)
                {
                    Assert.Equal("name" + test, property.Name);
                    Assert.Equal(test, property.Value.GetInt32());
                    test++;
                }

                structEnumerator.Reset();

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal("name0", structEnumerator.Current.Name);
                Assert.Equal(0, structEnumerator.Current.Value.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal("name1", structEnumerator.Current.Name);
                Assert.Equal(1, structEnumerator.Current.Value.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal("name2", structEnumerator.Current.Name);
                Assert.Equal(2, structEnumerator.Current.Value.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal("name3", structEnumerator.Current.Name);
                Assert.Equal(3, structEnumerator.Current.Value.GetInt32());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal("name2", strongBoxedEnumerator.Current.Name);
                Assert.Equal(2, strongBoxedEnumerator.Current.Value.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal("name4", structEnumerator.Current.Name);
                Assert.Equal(4, structEnumerator.Current.Value.GetInt32());

                Assert.True(structEnumerator.MoveNext());
                Assert.Equal("name5", structEnumerator.Current.Name);
                Assert.Equal(5, structEnumerator.Current.Value.GetInt32());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal("name3", strongBoxedEnumerator.Current.Name);
                Assert.Equal(3, strongBoxedEnumerator.Current.Value.GetInt32());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal("name2", ((JsonProperty)weakBoxedEnumerator.Current).Name);
                Assert.Equal(2, ((JsonProperty)weakBoxedEnumerator.Current).Value.GetInt32());

                Assert.False(structEnumerator.MoveNext());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal("name4", strongBoxedEnumerator.Current.Name);
                Assert.Equal(4, strongBoxedEnumerator.Current.Value.GetInt32());

                Assert.True(strongBoxedEnumerator.MoveNext());
                Assert.Equal("name5", strongBoxedEnumerator.Current.Name);
                Assert.Equal(5, strongBoxedEnumerator.Current.Value.GetInt32());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal("name3", ((JsonProperty)weakBoxedEnumerator.Current).Name);
                Assert.Equal(3, ((JsonProperty)weakBoxedEnumerator.Current).Value.GetInt32());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal("name4", ((JsonProperty)weakBoxedEnumerator.Current).Name);
                Assert.Equal(4, ((JsonProperty)weakBoxedEnumerator.Current).Value.GetInt32());

                Assert.False(structEnumerator.MoveNext());
                Assert.False(strongBoxedEnumerator.MoveNext());

                Assert.True(weakBoxedEnumerator.MoveNext());
                Assert.Equal("name5", ((JsonProperty)weakBoxedEnumerator.Current).Name);
                Assert.Equal(5, ((JsonProperty)weakBoxedEnumerator.Current).Value.GetInt32());

                Assert.False(weakBoxedEnumerator.MoveNext());
                Assert.False(structEnumerator.MoveNext());
                Assert.False(strongBoxedEnumerator.MoveNext());
                Assert.False(weakBoxedEnumerator.MoveNext());
            }
        }

        [Fact]
        public static void DefaultObjectEnumeratorDoesNotThrow()
        {
            JsonElement.ObjectEnumerator enumerable = default;
            JsonElement.ObjectEnumerator enumerator = enumerable.GetEnumerator();
            JsonElement.ObjectEnumerator defaultEnumerator = default;

            Assert.Equal(JsonValueKind.Undefined, enumerable.Current.Value.ValueKind);
            Assert.Equal(JsonValueKind.Undefined, enumerator.Current.Value.ValueKind);

            Assert.Throws<InvalidOperationException>(() => enumerable.Current.Name);
            Assert.Throws<InvalidOperationException>(() => enumerator.Current.Name);

            Assert.False(enumerable.MoveNext());
            Assert.False(enumerable.MoveNext());
            Assert.False(defaultEnumerator.MoveNext());
        }

        [Fact]
        public static void ReadNestedObject()
        {
            const string json = @"
{
  ""first"":
  {
    ""true"": true,
    ""false"": false,
    ""null"": null,
    ""int"": 3,
    ""nearlyPi"": 3.14159,
    ""text"": ""This is some text that does not end... <EOT>""
  },
  ""second"":
  {
    ""blub"": { ""bool"": true },
    ""glub"": { ""bool"": false }
  }
}";
            using (JsonDocument doc = JsonDocument.Parse(json))
            {
                JsonElement root = doc.RootElement;
                Assert.Equal(JsonValueKind.Object, root.ValueKind);

                Assert.True(root.GetProperty("first").GetProperty("true").GetBoolean());
                Assert.False(root.GetProperty("first").GetProperty("false").GetBoolean());
                Assert.Equal(JsonValueKind.Null, root.GetProperty("first").GetProperty("null").ValueKind);
                Assert.Equal(3, root.GetProperty("first").GetProperty("int").GetInt32());
                Assert.Equal(3.14159f, root.GetProperty("first").GetProperty("nearlyPi").GetSingle());
                Assert.Equal("This is some text that does not end... <EOT>", root.GetProperty("first").GetProperty("text").GetString());

                Assert.True(root.GetProperty("second").GetProperty("blub").GetProperty("bool").GetBoolean());
                Assert.False(root.GetProperty("second").GetProperty("glub").GetProperty("bool").GetBoolean());
            }
        }

        [Fact]
        public static void ParseNull()
        {
            Assert.Throws<ArgumentNullException>(
                "json",
                () => JsonDocument.Parse((string)null));

            AssertExtensions.Throws<ArgumentNullException>(
                "utf8Json",
                () => JsonDocument.Parse((Stream)null));

            // This synchronously throws the ArgumentNullException
            AssertExtensions.Throws<ArgumentNullException>(
                "utf8Json",
                () => { JsonDocument.ParseAsync(null); });
        }

        [Fact]
        public static void ParseDefaultReader()
        {
            Assert.ThrowsAny<JsonException>(() =>
            {
                Utf8JsonReader reader = default;
                JsonDocument.ParseValue(ref reader);
            });

            {
                Utf8JsonReader reader = default;
                Assert.False(JsonDocument.TryParseValue(ref reader, out JsonDocument document));
                Assert.Null(document);
            }
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtNumber(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(
                scenario,
                "963258741012365478965412325874125896325874123654789",
                JsonTokenType.Number,
                segmentCount);
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtString(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(
                scenario,
                "\"Now is the time for all good men to come to the aid of their country\"",
                JsonTokenType.String,
                segmentCount);
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtObject(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(
                scenario,
                "{ \"hello\": 3, \"world\": false }",
                JsonTokenType.StartObject,
                segmentCount);
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtArray(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(
                scenario,
                "[ { \"hello\": 3, \"world\": false }, 5, 2, 1, \"hello\", null, false, true ]",
                JsonTokenType.StartArray,
                segmentCount);
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtTrue(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(scenario, "true", JsonTokenType.True, segmentCount);
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtFalse(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(scenario, "false", JsonTokenType.False, segmentCount);
        }

        [Theory]
        [MemberData(nameof(ParseReaderSuccessfulCases))]
        public static void ParseReaderAtNull(ParseReaderScenario scenario, int segmentCount)
        {
            ParseReaderInScenario(scenario, "null", JsonTokenType.Null, segmentCount);
        }

        private static void ParseReaderInScenario(
            ParseReaderScenario scenario,
            string valueJson,
            JsonTokenType tokenType,
            int segmentCount)
        {
            switch (scenario)
            {
                case ParseReaderScenario.NotStarted:
                case ParseReaderScenario.StartAtFirstToken:
                    ParseReaderAtStart(
                        scenario == ParseReaderScenario.StartAtFirstToken,
                        valueJson,
                        tokenType,
                        segmentCount);
                    break;
                case ParseReaderScenario.StartAtNestedValue:
                    ParseReaderAtNestedValue(valueJson, tokenType, segmentCount);
                    break;
                case ParseReaderScenario.StartAtPropertyName:
                    ParseReaderAtPropertyName(valueJson, tokenType, segmentCount);
                    break;
            }
        }

        private static void ParseReaderAtStart(
            bool readFirst,
            string valueJson,
            JsonTokenType tokenType,
            int segmentCount)
        {
            string json = $"      {valueJson}   5";
            byte[] utf8 = Encoding.UTF8.GetBytes(json);
            JsonReaderState state = default;

            BuildSegmentedReader(out Utf8JsonReader reader, utf8, segmentCount, state);

            if (readFirst)
            {
                Assert.True(reader.Read(), "Move to first token");
            }

            using (JsonDocument document = JsonDocument.ParseValue(ref reader))
            {
                Assert.Equal(valueJson, document.RootElement.GetRawText());
            }

            Exception ex;
            long position = reader.BytesConsumed;

            try
            {
                reader.Read();
                ex = null;
            }
            catch (Exception e)
            {
                ex = e;
            }

            Assert.NotNull(ex);
            Assert.IsAssignableFrom<JsonException>(ex);

            BuildSegmentedReader(out reader, utf8.AsMemory((int)position), 0, state, true);

            Assert.True(reader.Read(), "Read trailing number");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
        }

        private static void ParseReaderAtNestedValue(
            string valueJson,
            JsonTokenType tokenType,
            int segmentCount)
        {
            // Open-ended value (missing final ])
            string json = $"[ 0, 1, 2, 3, 4, {valueJson}     , 6, 7, 8, 9 ";
            byte[] utf8 = Encoding.UTF8.GetBytes(json);
            JsonReaderState state = default;

            BuildSegmentedReader(out Utf8JsonReader reader, utf8, segmentCount, state);

            Assert.True(reader.Read(), "Read [");
            Assert.Equal(JsonTokenType.StartArray, reader.TokenType);
            Assert.True(reader.Read(), "Read 0");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 1");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 2");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 3");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 4");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);

            Assert.True(reader.Read(), "Read desired value");
            Assert.Equal(tokenType, reader.TokenType);
            long currentPosition = reader.BytesConsumed;

            using (JsonDocument document = JsonDocument.ParseValue(ref reader))
            {
                Assert.Equal(valueJson, document.RootElement.GetRawText());
            }

            switch (tokenType)
            {
                case JsonTokenType.StartArray:
                    Assert.Equal(JsonTokenType.EndArray, reader.TokenType);
                    Assert.InRange(reader.BytesConsumed, currentPosition + 1, long.MaxValue);
                    break;
                case JsonTokenType.StartObject:
                    Assert.Equal(JsonTokenType.EndObject, reader.TokenType);
                    Assert.InRange(reader.BytesConsumed, currentPosition + 1, long.MaxValue);
                    break;
                default:
                    Assert.Equal(tokenType, reader.TokenType);
                    Assert.Equal(currentPosition, reader.BytesConsumed);
                    break;
            }

            Assert.True(reader.Read(), "Read 6");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 7");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 8");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 9");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);

            Assert.False(reader.Read());
        }

        private static void ParseReaderAtPropertyName(
            string valueJson,
            JsonTokenType tokenType,
            int segmentCount)
        {
            // Open-ended value
            string json = $"{{ \"this\": [ {{ \"is\": {{ \"the\": {{ \"target\": {valueJson}    , \"capiche\" : [ 6, 7, 8, 9 ] ";
            byte[] utf8 = Encoding.UTF8.GetBytes(json);
            JsonReaderState state = default;

            BuildSegmentedReader(out Utf8JsonReader reader, utf8, segmentCount, state);

            Assert.True(reader.Read(), "Read root object start");
            Assert.Equal(JsonTokenType.StartObject, reader.TokenType);
            Assert.True(reader.Read(), "Read \"this\" property name");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.True(reader.Read(), "Read \"this\" StartArray");
            Assert.Equal(JsonTokenType.StartArray, reader.TokenType);
            Assert.True(reader.Read(), "Read \"this\" StartObject");
            Assert.Equal(JsonTokenType.StartObject, reader.TokenType);
            Assert.True(reader.Read(), "Read \"is\" property name");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.True(reader.Read(), "Read \"is\" StartObject");
            Assert.Equal(JsonTokenType.StartObject, reader.TokenType);
            Assert.True(reader.Read(), "Read \"the\" property name");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.True(reader.Read(), "Read \"the\" StartObject");
            Assert.Equal(JsonTokenType.StartObject, reader.TokenType);
            Assert.True(reader.Read(), "Read \"target\" property name");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);

            long currentPosition = reader.BytesConsumed;

            using (JsonDocument document = JsonDocument.ParseValue(ref reader))
            {
                Assert.Equal(valueJson, document.RootElement.GetRawText());
            }

            // The reader will have moved.
            Assert.InRange(reader.BytesConsumed, currentPosition + 1, long.MaxValue);

            switch (tokenType)
            {
                case JsonTokenType.StartArray:
                    Assert.Equal(JsonTokenType.EndArray, reader.TokenType);
                    break;
                case JsonTokenType.StartObject:
                    Assert.Equal(JsonTokenType.EndObject, reader.TokenType);
                    break;
                default:
                    Assert.Equal(tokenType, reader.TokenType);
                    break;
            }

            Assert.True(reader.Read(), "Read \"capiche\" property name");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.True(reader.Read(), "Read \"capiche\" StartArray");
            Assert.Equal(JsonTokenType.StartArray, reader.TokenType);
            Assert.True(reader.Read(), "Read 6");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 7");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 8");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read 9");
            Assert.Equal(JsonTokenType.Number, reader.TokenType);
            Assert.True(reader.Read(), "Read \"capiche\" EndArray");
            Assert.Equal(JsonTokenType.EndArray, reader.TokenType);

            Assert.False(reader.Read());
        }

        [Theory]
        [InlineData(JsonTokenType.EndObject)]
        [InlineData(JsonTokenType.EndArray)]
        public static void ParseReaderAtInvalidStart(JsonTokenType tokenType)
        {
            string jsonString;

            switch (tokenType)
            {
                case JsonTokenType.EndObject:
                    jsonString = "[ { } ]";
                    break;
                case JsonTokenType.EndArray:
                    jsonString = "[ [ ] ]";
                    break;
                default:
                    throw new ArgumentOutOfRangeException(nameof(tokenType));
            }

            byte[] jsonUtf8 = Encoding.UTF8.GetBytes(jsonString);
            JsonReaderState state = default;

            Utf8JsonReader reader = new Utf8JsonReader(
                jsonUtf8,
                isFinalBlock: false,
                state);

            Assert.True(reader.Read(), "Move to first [");
            Assert.True(reader.Read(), "Move to internal start");
            Assert.True(reader.Read(), "Move to internal end");

            Exception ex;

            long initialPosition = reader.BytesConsumed;

            try
            {
                using (JsonDocument.ParseValue(ref reader))
                {
                }

                ex = null;
            }
            catch (Exception e)
            {
                ex = e;
            }

            Assert.NotNull(ex);
            Assert.IsAssignableFrom<JsonException>(ex);

            Assert.Equal(initialPosition, reader.BytesConsumed);

            Assert.False(JsonDocument.TryParseValue(ref reader, out JsonDocument doc));
            Assert.Null(doc);
            Assert.Equal(initialPosition, reader.BytesConsumed);
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(2)]
        [InlineData(15)]
        public static void ParseReaderThrowsWhileReading(int segmentCount)
        {
            const string jsonString = "[ { \"this\": { \"is\": [ invalid ] } } ]";
            byte[] utf8Json = Encoding.UTF8.GetBytes(jsonString);
            JsonReaderState state = default;

            BuildSegmentedReader(out Utf8JsonReader reader, utf8Json, segmentCount, state);

            Assert.True(reader.Read(), "Read StartArray");
            Assert.True(reader.Read(), "Read StartObject");
            Assert.True(reader.Read(), "Read \"this\" PropertyName");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            long startPosition = reader.BytesConsumed;

            Exception ex;

            try
            {
                using (JsonDocument.ParseValue(ref reader))
                {
                }

                ex = null;
            }
            catch (Exception e)
            {
                ex = e;
            }

            Assert.NotNull(ex);
            Assert.IsAssignableFrom<JsonException>(ex);

            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.Equal(startPosition, reader.BytesConsumed);

            JsonDocument doc = null;

            try
            {
                JsonDocument.TryParseValue(ref reader, out doc);
                ex = null;
            }
            catch (Exception e)
            {
                ex = e;
            }

            Assert.NotNull(ex);
            Assert.IsAssignableFrom<JsonException>(ex);
            Assert.Null(doc);

            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.Equal(startPosition, reader.BytesConsumed);
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(2)]
        [InlineData(15)]
        public static void ParseReaderTerminatesWhileReading(int segmentCount)
        {
            const string jsonString = "[ { \"this\": { \"is\": [ ";
            byte[] utf8Json = Encoding.UTF8.GetBytes(jsonString);
            JsonReaderState state = default;

            BuildSegmentedReader(out Utf8JsonReader reader, utf8Json, segmentCount, state);

            Assert.True(reader.Read(), "Read StartArray");
            Assert.True(reader.Read(), "Read StartObject");
            Assert.True(reader.Read(), "Read \"this\" PropertyName");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            long startPosition = reader.BytesConsumed;

            Exception ex;

            try
            {
                using (JsonDocument.ParseValue(ref reader))
                {
                }

                ex = null;
            }
            catch (Exception e)
            {
                ex = e;
            }

            Assert.NotNull(ex);
            Assert.IsAssignableFrom<JsonException>(ex);

            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.Equal(startPosition, reader.BytesConsumed);

            Assert.False(JsonDocument.TryParseValue(ref reader, out JsonDocument doc));
            Assert.Null(doc);

            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.Equal(startPosition, reader.BytesConsumed);
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(2)]
        [InlineData(15)]
        public static void ParseReaderTerminatesAfterPropertyName(int segmentCount)
        {
            const string jsonString = "[ { \"this\": ";
            byte[] utf8Json = Encoding.UTF8.GetBytes(jsonString);
            JsonReaderState state = default;

            BuildSegmentedReader(out Utf8JsonReader reader, utf8Json, segmentCount, state);

            Assert.True(reader.Read(), "Read StartArray");
            Assert.True(reader.Read(), "Read StartObject");
            Assert.True(reader.Read(), "Read \"this\" PropertyName");
            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            long startPosition = reader.BytesConsumed;

            Exception ex;

            try
            {
                using (JsonDocument.ParseValue(ref reader))
                {
                }

                ex = null;
            }
            catch (Exception e)
            {
                ex = e;
            }

            Assert.NotNull(ex);
            Assert.IsAssignableFrom<JsonException>(ex);

            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.Equal(startPosition, reader.BytesConsumed);

            Assert.False(JsonDocument.TryParseValue(ref reader, out JsonDocument doc));
            Assert.Null(doc);

            Assert.Equal(JsonTokenType.PropertyName, reader.TokenType);
            Assert.Equal(startPosition, reader.BytesConsumed);
        }

        [Fact]
        public static void ParseValue_AllowMultipleValues_TrailingJson()
        {
            var options = new JsonReaderOptions { AllowMultipleValues = true };
            var reader = new Utf8JsonReader("[null,false,42,{},[1]]             [43]"u8, options);

            using JsonDocument doc1 = JsonDocument.ParseValue(ref reader);
            Assert.Equal("[null,false,42,{},[1]]", doc1.RootElement.GetRawText());
            Assert.Equal(JsonTokenType.EndArray, reader.TokenType);

            Assert.True(reader.Read());
            using JsonDocument doc2 = JsonDocument.ParseValue(ref reader);
            Assert.Equal("[43]", doc2.RootElement.GetRawText());

            Assert.False(reader.Read());
        }


        [Fact]
        public static void ParseValue_AllowMultipleValues_TrailingContent()
        {
            var options = new JsonReaderOptions { AllowMultipleValues = true };
            var reader = new Utf8JsonReader("[null,false,42,{},[1]]             <NotJson/>"u8, options);

            using JsonDocument doc = JsonDocument.ParseValue(ref reader);
            Assert.Equal("[null,false,42,{},[1]]", doc.RootElement.GetRawText());
            Assert.Equal(JsonTokenType.EndArray, reader.TokenType);

            JsonTestHelper.AssertThrows<JsonException>(ref reader, (ref Utf8JsonReader reader) => reader.Read());
        }

        [Theory]
        [InlineData("""{ "foo" : [1], "test": false, "bar" : { "nested": 3 } }""", 3)]
        [InlineData("""{ "foo" : [1,2,3,4] }""", 1)]
        [InlineData("""{}""", 0)]
        [InlineData("""{ "foo" : {"nested:" : {"nested": 1, "bla": [1, 2, {"bla": 3}] } }, "test": true, "foo2" : {"nested:" : {"nested": 1, "bla": [1, 2, {"bla": 3}] } }}""", 3)]
        public static void TestGetPropertyCount(string json, int expectedCount)
        {
            JsonElement element = JsonElement.Parse(json);
            Assert.Equal(expectedCount, element.GetPropertyCount());
        }

        [Fact]
        public static void VerifyGetPropertyCountAndArrayLengthUsingEnumerateMethods()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.ProjectLockJson))
            {
                CheckPropertyCountAndArrayLengthAgainstEnumerateMethods(doc.RootElement);
            }

            void CheckPropertyCountAndArrayLengthAgainstEnumerateMethods(JsonElement elem)
            {
                if (elem.ValueKind == JsonValueKind.Object)
                {
                    Assert.Equal(elem.EnumerateObject().Count(), elem.GetPropertyCount());
                    foreach (JsonProperty prop in elem.EnumerateObject())
                    {
                        CheckPropertyCountAndArrayLengthAgainstEnumerateMethods(prop.Value);
                    }
                }
                else if (elem.ValueKind == JsonValueKind.Array)
                {
                    Assert.Equal(elem.EnumerateArray().Count(), elem.GetArrayLength());
                    foreach (JsonElement item in elem.EnumerateArray())
                    {
                        CheckPropertyCountAndArrayLengthAgainstEnumerateMethods(item);
                    }
                }
            }
        }

        [Fact]
        public static void EnsureResizeSucceeds()
        {
            // This test increases coverage, so it's based on a lot of implementation detail,
            // to ensure that the otherwise untested blocks produce the right functional behavior.
            //
            // The initial database size is just over the number of bytes of UTF-8 data in the payload,
            // capped at 2^20 (unless the payload exceeds 2^22).
            //
            // Regrowth happens if the rented array (which may be bigger than we asked for) is not sufficient,
            // meaning tokens (on average) occur more often than every 12 bytes.
            //
            // The array pool (for bytes) returns power-of-two sizes.
            //
            // Conclusion: A resize will happen if a payload of 1MB+epsilon has tokens more often than every 12 bytes.
            //
            // Integer numbers as strings, padded to 4 with no whitespace in series in an array: 7x + 1.
            //  That would take 149797 integers.
            //
            // Padded to 5 (8x + 1) => 131072 integers.
            // Padded to 6 (9x + 1) => 116509 integers.
            //
            // At pad-to-6 tokens occur every 9 bytes, and we can represent values without repeat.

            const int NumberOfNumbers = (1024 * 1024 / 9) + 1;
            const int NumberOfBytes = 9 * NumberOfNumbers + 1;

            byte[] utf8Json = new byte[NumberOfBytes];
            utf8Json.AsSpan().Fill((byte)'"');
            utf8Json[0] = (byte)'[';

            Span<byte> valuesSpan = utf8Json.AsSpan(1);
            StandardFormat format = StandardFormat.Parse("D6");

            for (int i = 0; i < NumberOfNumbers; i++)
            {
                // Just inside the quote
                Span<byte> curDest = valuesSpan.Slice(9 * i + 1);

                if (!Utf8Formatter.TryFormat(i, curDest, out int bytesWritten, format) || bytesWritten != 6)
                {
                    throw new InvalidOperationException("" + i);
                }

                curDest[7] = (byte)',';
            }

            // Replace last comma with ]
            utf8Json[NumberOfBytes - 1] = (byte)']';

            using (JsonDocument doc = JsonDocument.Parse(utf8Json))
            {
                JsonElement root = doc.RootElement;
                int count = root.GetArrayLength();

                for (int i = 0; i < count; i++)
                {
                    Assert.Equal(i, int.Parse(root[i].GetString()));
                }
            }
        }

        [Fact]
        public static void ValueEquals_Null_TrueForNullFalseForEmpty()
        {
            using (JsonDocument doc = JsonDocument.Parse("   null   "))
            {
                JsonElement jElement = doc.RootElement;
                Assert.True(jElement.ValueEquals((string)null));
                Assert.True(jElement.ValueEquals(default(ReadOnlySpan<char>)));
                Assert.True(jElement.ValueEquals((ReadOnlySpan<byte>)null));
                Assert.True(jElement.ValueEquals(default(ReadOnlySpan<byte>)));

                Assert.False(jElement.ValueEquals(Array.Empty<byte>()));
                Assert.False(jElement.ValueEquals(""));
                Assert.False(jElement.ValueEquals("".AsSpan()));
            }
        }

        [Fact]
        public static void ValueEquals_EmptyJsonString_True()
        {
            using (JsonDocument doc = JsonDocument.Parse("\"\""))
            {
                JsonElement jElement = doc.RootElement;
                Assert.True(jElement.ValueEquals(""));
                Assert.True(jElement.ValueEquals("".AsSpan()));
                Assert.True(jElement.ValueEquals(default(ReadOnlySpan<char>)));
                Assert.True(jElement.ValueEquals(default(ReadOnlySpan<byte>)));
                Assert.True(jElement.ValueEquals(Array.Empty<byte>()));
            }
        }

        [Fact]
        public static void ValueEquals_TestTextEqualsLargeMatch_True()
        {
            string lookup = new string('a', 320);
            string jsonString = "\"" + lookup + "\"";
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                Assert.True(jElement.ValueEquals((string)lookup));
                Assert.True(jElement.ValueEquals((ReadOnlySpan<char>)lookup.AsSpan()));
            }
        }

        [Theory]
        [InlineData("\"conne\\u0063tionId\"", "connectionId")]
        [InlineData("\"connectionId\"", "connectionId")]
        [InlineData("\"123\"", "123")]
        [InlineData("\"My name is \\\"Ahson\\\"\"", "My name is \"Ahson\"")]
        public static void ValueEquals_JsonTokenStringType_True(string jsonString, string expected)
        {
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                Assert.True(jElement.ValueEquals(expected));
                Assert.True(jElement.ValueEquals(expected.AsSpan()));
                byte[] expectedGetBytes = Encoding.UTF8.GetBytes(expected);
                Assert.True(jElement.ValueEquals(expectedGetBytes));
            }
        }

        [Theory]
        [InlineData("\"conne\\u0063tionId\"", "c")]
        public static void ValueEquals_DestinationTooSmallComparesEscaping_False(string jsonString, string other)
        {
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                Assert.False(jElement.ValueEquals(other));
                Assert.False(jElement.ValueEquals(other.AsSpan()));
                byte[] otherGetBytes = Encoding.UTF8.GetBytes(other);
                Assert.False(jElement.ValueEquals(otherGetBytes));
            }
        }

        [Theory]
        [InlineData("\"hello\"", new char[1] { (char)0xDC01 })]    // low surrogate - invalid
        [InlineData("\"hello\"", new char[1] { (char)0xD801 })]    // high surrogate - missing pair
        public static void InvalidUTF16Search(string jsonString, char[] lookup)
        {
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                Assert.False(jElement.ValueEquals(lookup));
            }
        }

        [Theory]
        [InlineData("\"connectionId\"", "\"conne\\u0063tionId\"")]
        [InlineData("\"conne\\u0063tionId\"", "connecxionId")] // intentionally making mismatch after escaped character
        [InlineData("\"conne\\u0063tionId\"", "bonnectionId")] // intentionally changing the expected starting character
        public static void ValueEquals_JsonTokenStringType_False(string jsonString, string otherText)
        {
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                Assert.False(jElement.ValueEquals(otherText));
                Assert.False(jElement.ValueEquals(otherText.AsSpan()));
                byte[] expectedGetBytes = Encoding.UTF8.GetBytes(otherText);
                Assert.False(jElement.ValueEquals(expectedGetBytes));
            }
        }

        [Theory]
        [InlineData("{}")]
        [InlineData("{\"\" : \"\"}")]
        [InlineData("{\"sample\" : \"\"}")]
        [InlineData("{\"sample\" : null}")]
        [InlineData("{\"sample\" : \"sample-value\"}")]
        [InlineData("{\"connectionId\" : \"123\"}")]
        public static void ValueEquals_NotString_Throws(string jsonString)
        {
            const string ErrorMessage = "The requested operation requires an element of type 'String', but the target element has type 'Object'.";
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                const string ThrowsAnyway = "throws-anyway";
                AssertExtensions.Throws<InvalidOperationException>(() => jElement.ValueEquals(ThrowsAnyway), ErrorMessage);
                AssertExtensions.Throws<InvalidOperationException>(() => jElement.ValueEquals(ThrowsAnyway.AsSpan()), ErrorMessage);
                AssertExtensions.Throws<InvalidOperationException>(() => jElement.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)), ErrorMessage);
            }
        }

        [Fact]
        public static void NameEquals_Empty_Throws()
        {
            const string jsonString = "{\"\" : \"some-value\"}";
            const string ErrorMessage = "The requested operation requires an element of type 'String', but the target element has type 'Object'.";
            using (JsonDocument doc = JsonDocument.Parse(jsonString))
            {
                JsonElement jElement = doc.RootElement;
                const string ThrowsAnyway = "throws-anyway";
                AssertExtensions.Throws<InvalidOperationException>(() => jElement.ValueEquals(ThrowsAnyway), ErrorMessage);
                AssertExtensions.Throws<InvalidOperationException>(() => jElement.ValueEquals(ThrowsAnyway.AsSpan()), ErrorMessage);
                AssertExtensions.Throws<InvalidOperationException>(() => jElement.ValueEquals(Encoding.UTF8.GetBytes(ThrowsAnyway)), ErrorMessage);
            }
        }

        [ConditionalFact(typeof(PlatformDetection), nameof(PlatformDetection.IsThreadingSupported))]
        [OuterLoop] // thread-safety / stress test
        public static async Task GetString_ConcurrentUse_ThreadSafe()
        {
            using (JsonDocument doc = JsonDocument.Parse(SR.SimpleObjectJson))
            {
                JsonElement first = doc.RootElement.GetProperty("first");
                JsonElement last = doc.RootElement.GetProperty("last");

                const int Iters = 10_000;
                using (var gate = new Barrier(2))
                {
                    await Task.WhenAll(
                        Task.Factory.StartNew(() =>
                        {
                            gate.SignalAndWait();
                            for (int i = 0; i < Iters; i++)
                            {
                                Assert.Equal("John", first.GetString());
                                Assert.True(first.ValueEquals("John"));
                            }
                        }, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default),
                        Task.Factory.StartNew(() =>
                        {
                            gate.SignalAndWait();
                            for (int i = 0; i < Iters; i++)
                            {
                                Assert.Equal("Smith", last.GetString());
                                Assert.True(last.ValueEquals("Smith"));
                            }
                        }, CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default));
                }
            }
        }

        private static void BuildSegmentedReader(
            out Utf8JsonReader reader,
            ReadOnlyMemory<byte> data,
            int segmentCount,
            scoped in JsonReaderState state,
            bool isFinalBlock = false)
        {
            if (segmentCount == 0)
            {
                reader = new Utf8JsonReader(
                    data.Span,
                    isFinalBlock,
                    state);
            }
            else if (segmentCount == 1)
            {
                reader = new Utf8JsonReader(
                    new ReadOnlySequence<byte>(data),
                    isFinalBlock,
                    state);
            }
            else
            {
                reader = new Utf8JsonReader(
                    JsonTestHelper.SegmentInto(data, segmentCount),
                    isFinalBlock,
                    state);
            }

        }

        private static string GetExpectedConcat(TestCaseType testCaseType, string jsonString)
        {
            if (s_expectedConcat.TryGetValue(testCaseType, out string existing))
            {
                return existing;
            }

            TextReader reader = new StringReader(jsonString);
            return s_expectedConcat[testCaseType] = JsonTestHelper.NewtonsoftReturnStringHelper(reader);
        }

        private static string GetCompactJson(TestCaseType testCaseType, string jsonString)
        {
            if (s_compactJson.TryGetValue(testCaseType, out string existing))
            {
                return existing;
            }

            using (var jsonReader = new JsonTextReader(new StringReader(jsonString)) { MaxDepth = null })
            {
                jsonReader.FloatParseHandling = FloatParseHandling.Decimal;
                JToken jtoken = JToken.ReadFrom(jsonReader);
                var stringWriter = new StringWriter();

                using (JsonTextWriter jsonWriter = new JsonTextWriter(stringWriter))
                {
                    jtoken.WriteTo(jsonWriter);
                    existing = stringWriter.ToString();
                }
            }

            return s_compactJson[testCaseType] = existing;
        }

        [Fact]
        public static async Task VerifyMultiThreadedDispose()
        {
            Action<object> disposeAction = (object document) => ((JsonDocument)document).Dispose();

            // Create a bunch of parallel tasks that call Dispose several times on the same object.
            Task[] tasks = new Task[100];
            int count = 0;
            for (int j = 0; j < 10; j++)
            {
                JsonDocument document = JsonDocument.Parse("123" + j);
                for (int i = 0; i < 10; i++)
                {
                    tasks[count] = new Task(disposeAction, document);
                    tasks[count].Start();

                    count++;
                }
            }

            await Task.WhenAll(tasks);

            // When ArrayPool gets corrupted, the Rent method might return an already rented array, which is incorrect.
            // So we will rent as many arrays as calls to JsonElement.Dispose and check they are unique.
            // The minimum length that we ask for is a mirror of the size of the string passed to JsonDocument.Parse.
            HashSet<byte[]> uniqueAddresses = new HashSet<byte[]>();
            while (count > 0)
            {
                byte[] arr = ArrayPool<byte>.Shared.Rent(4);
                Assert.True(uniqueAddresses.Add(arr));
                count--;
            }
        }

        [Fact]
        public static void DeserializeNullAsNullLiteral()
        {
            var jsonDocument = JsonSerializer.Deserialize<JsonDocument>("null");
            Assert.NotNull(jsonDocument);
            Assert.Equal(JsonValueKind.Null, jsonDocument.RootElement.ValueKind);
        }

        [Fact]
        public static void JsonMarshal_GetRawUtf8Value_DisposedDocument_ThrowsObjectDisposedException()
        {
            JsonDocument jDoc = JsonDocument.Parse("{}");
            JsonElement element = jDoc.RootElement;
            jDoc.Dispose();

            Assert.Throws<ObjectDisposedException>(() => JsonMarshal.GetRawUtf8Value(element));
        }

        [Theory]
        [MemberData(nameof(TestData.DuplicatePropertyJsonPayloads), MemberType = typeof(TestData))]
        public static void ParseJsonDocumentWithDuplicateProperties(string jsonPayload, bool isValidJson = false)
        {
            AssertDuplicateProperty(jsonPayload, isValidJson);
        }

        [Theory]
        [MemberData(nameof(TestData.DuplicatePropertyJsonPayloads), MemberType = typeof(TestData))]
        public static void ParseJsonDocumentArrayWithDuplicateProperties(string jsonPayload, bool isValidJson = false)
        {
            jsonPayload = $"[{jsonPayload}]";
            AssertDuplicateProperty(jsonPayload, isValidJson);
        }

        [Theory]
        [MemberData(nameof(TestData.DuplicatePropertyJsonPayloads), MemberType = typeof(TestData))]
        public static void ParseJsonDocumentDeeplyNestedWithDuplicateProperties(string jsonPayload, bool isValidJson = false)
        {
            jsonPayload = $$"""{"p0":{"p1":{"p2":{"p3":{"p4":{"p5":{"p6":{"p7":{"p8":{"p9":{{jsonPayload}}} } } } } } } } } }""";
            AssertDuplicateProperty(jsonPayload, isValidJson);
        }

        [Theory]
        [MemberData(nameof(TestData.DuplicatePropertyJsonPayloads), MemberType = typeof(TestData))]
        public static void ParseJsonDocumentClassWithDuplicateProperties(string jsonPayload, bool isValidJson = false)
        {
            jsonPayload = $$"""{"Object":{{jsonPayload}}}""";
            AssertDuplicateProperty(jsonPayload, isValidJson);
        }

        [Theory]
        [InlineData(3, 0)]
        [InlineData(3, 2)]
        [InlineData(100, 0)]
        [InlineData(100, 99)]
        public static void ParseJsonDocumentObjectWithDuplicateProperties(int count, int insertPosition)
        {
            string json = CreatePayload(count, insertPosition, "p1");
            AssertDuplicateProperty(json, isValidJson: false);
        }

        [Theory]
        [InlineData(3, 0)]
        [InlineData(3, 2)]
        [InlineData(100, 0)]
        [InlineData(100, 99)]
        public static void ParseJsonDocumentEscapeWithDuplicateProperties(int count, int insertPosition)
        {
            string json = CreatePayload(count, insertPosition, """p\u0031""");
            AssertDuplicateProperty(json, isValidJson: false);
        }

        [Theory]
        [InlineData(3, 0)]
        [InlineData(3, 2)]
        [InlineData(100, 0)]
        [InlineData(100, 99)]
        public static void ParseJsonDocumentObjectWithNoDuplicateProperties(int count, int insertPosition)
        {
            string json = CreatePayload(count, insertPosition, "notduplicate");
            AssertDuplicateProperty(json, isValidJson: true);
        }

        [Theory]
        [InlineData(3, 0)]
        [InlineData(3, 2)]
        [InlineData(100, 0)]
        [InlineData(100, 99)]
        public static void ParseJsonDocumentEscapeWithNoDuplicateProperties(int count, int insertPosition)
        {
            string json = CreatePayload(count, insertPosition, """notduplicate\u0030\u0030""");
            AssertDuplicateProperty(json, isValidJson: true);
        }

        private static string CreatePayload(int count, int insertPosition, string insertProperty)
        {
            StringBuilder builder = new();
            builder.Append("{");
            for (int i = 0; i < count; i++)
            {
                if (i > 0)
                    builder.AppendLine(",");

                if (i == insertPosition)
                    builder.Append($"""  "{insertProperty}":1""");
                else
                    builder.Append($"""  "p{i}":1""");
            }
            builder.Append("}");
            return builder.ToString();
        }

        private static void AssertDuplicateProperty(string jsonPayload, bool isValidJson)
        {
            if (isValidJson)
            {
                using (JsonDocument.Parse(jsonPayload, s_noDuplicateParamsOptions)) { } // Assert no throw
            }
            else
            {
                Assert.Throws<JsonException>(() => JsonDocument.Parse(jsonPayload, s_noDuplicateParamsOptions));
            }

            using (JsonDocument.Parse(jsonPayload)) { } // Assert no throw
        }

        [Fact]
        public static void ParseJsonDuplicatePropertiesErrorMessage()
        {
            string json = """{"a":1,"a":1}""";
            AssertExtensions.ThrowsContains<JsonException>(
                () => JsonDocument.Parse(json, s_noDuplicateParamsOptions),
                "a");
        }

        [Fact]
        public static void ParseJsonDuplicatePropertiesErrorMessageLong()
        {
            string json = """{"12345678901234567":1,"12345678901234567":1}""";
            AssertExtensions.ThrowsContains<JsonException>(
                () => JsonDocument.Parse(json, s_noDuplicateParamsOptions),
                "123456789012345...");

            json = """{"123456789012345":1,"123456789012345":1}""";
            AssertExtensions.ThrowsContains<JsonException>(
                () => JsonDocument.Parse(json, s_noDuplicateParamsOptions),
                "123456789012345");

            json = """{"1234567890123456":1,"1234567890123456":1}""";
            AssertExtensions.ThrowsContains<JsonException>(
                () => JsonDocument.Parse(json, s_noDuplicateParamsOptions),
                "123456789012345...");
        }

        [Fact]
        public static void ParseJsonDuplicatePropertiesErrorMessageEscaped()
        {
            string json = """{"0":1,"\u0030":1}""";
            AssertExtensions.ThrowsContains<JsonException>(
                () => JsonDocument.Parse(json, s_noDuplicateParamsOptions),
                "'\u0030'");

            json = """{"\u0030":1,"0":1}""";
            AssertExtensions.ThrowsContains<JsonException>(
                () => JsonDocument.Parse(json, s_noDuplicateParamsOptions),
                "'0'");
        }
    }

    public class ThrowOnReadStream : MemoryStream
    {
        public ThrowOnReadStream(byte[] bytes) : base(bytes)
        {
        }

        public override int Read(byte[] buffer, int offset, int count)
        {
            throw new EndOfStreamException();
        }
    }

    public class ThrowOnCanSeekStream : MemoryStream
    {
        public ThrowOnCanSeekStream(byte[] bytes) : base(bytes)
        {
        }

        public override bool CanSeek => throw new InsufficientMemoryException();
    }
}
