// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Linq;
using Xunit;

namespace System.ComponentModel.TypeConverterTests
{
    public class SizeConverterTests : StringTypeConverterTestBase<Size>
    {
        protected override TypeConverter Converter { get; } = new SizeConverter();
        protected override bool StandardValuesSupported { get; } = false;
        protected override bool StandardValuesExclusive { get; } = false;
        protected override Size Default => new Size(1, 1);
        protected override bool CreateInstanceSupported { get; } = true;
        protected override bool IsGetPropertiesSupported { get; } = true;

        protected override IEnumerable<Tuple<Size, Dictionary<string, object>>> CreateInstancePairs
        {
            get
            {
                yield return Tuple.Create(new Size(10, 20), new Dictionary<string, object>
                {
                    ["Width"] = 10,
                    ["Height"] = 20,
                });
                yield return Tuple.Create(new Size(-2, 3), new Dictionary<string, object>
                {
                    ["Width"] = -2,
                    ["Height"] = 3,
                });
            }
        }

        [Theory]
        [InlineData(typeof(string))]
        public void CanConvertFromTrue(Type type)
        {
            CanConvertFrom(type);
        }

        [Theory]
        [InlineData(typeof(Rectangle))]
        [InlineData(typeof(RectangleF))]
        [InlineData(typeof(Point))]
        [InlineData(typeof(PointF))]
        [InlineData(typeof(Color))]
        [InlineData(typeof(Size))]
        [InlineData(typeof(SizeF))]
        [InlineData(typeof(object))]
        [InlineData(typeof(int))]
        public void CanConvertFromFalse(Type type)
        {
            CannotConvertFrom(type);
        }

        [Theory]
        [InlineData(typeof(string))]
        public void CanConvertToTrue(Type type)
        {
            CanConvertTo(type);
        }

        [Theory]
        [InlineData(typeof(Rectangle))]
        [InlineData(typeof(RectangleF))]
        [InlineData(typeof(Point))]
        [InlineData(typeof(PointF))]
        [InlineData(typeof(Color))]
        [InlineData(typeof(Size))]
        [InlineData(typeof(SizeF))]
        [InlineData(typeof(object))]
        [InlineData(typeof(int))]
        public void CanConvertToFalse(Type type)
        {
            CannotConvertTo(type);
        }

        public static IEnumerable<object[]> SizeData =>
            [
                [0, 1],
                [1, 0],
                [-1, 1],
                [1, -1],
                [-1, -2],
                [int.MaxValue, int.MaxValue - 1],
                [int.MinValue, int.MaxValue],
                [int.MaxValue, int.MinValue],
                [int.MinValue, int.MinValue + 1],
            ];

        [Theory]
        [MemberData(nameof(SizeData))]
        public void ConvertFrom(int width, int height)
        {
            TestConvertFromString(new Size(width, height), $"{width}, {height}");
        }

        [Theory]
        [InlineData("1")]
        [InlineData("*1")]
        [InlineData("1, 2, 3")]
        [InlineData("*1, 2, 3")]
        public void ConvertFrom_ArgumentException(string value)
        {
            ConvertFromThrowsArgumentExceptionForString(value);
        }

        [Fact]
        public void ConvertFrom_Invalid()
        {
            ConvertFromThrowsFormatInnerExceptionForString("*1, 1");
        }

        [Theory]
        [InlineData("")]
        [InlineData(" \t ")]
        public void ConvertFrom_WhiteSpace(string value)
        {
            Assert.Null(Converter.ConvertFromString(value));
        }

        public static IEnumerable<object[]> ConvertFrom_NotSupportedData =>
            new[]
            {
                new object[] {new Point(1, 1)},
                new object[] {new PointF(1, 1)},
                new object[] {new Size(1, 1)},
                new object[] {new SizeF(1, 1)},
                new object[] {0x10},
            };

        [Theory]
        [MemberData(nameof(ConvertFrom_NotSupportedData))]
        public void ConvertFrom_NotSupported(object value)
        {
            ConvertFromThrowsNotSupportedFor(value);
        }

        [Theory]
        [MemberData(nameof(SizeData))]
        public void ConvertTo(int width, int height)
        {
            TestConvertToString(new Size(width, height), $"{width}, {height}");
        }

        [Theory]
        [InlineData(typeof(Size))]
        [InlineData(typeof(SizeF))]
        [InlineData(typeof(Point))]
        [InlineData(typeof(PointF))]
        [InlineData(typeof(int))]
        public void ConvertTo_NotSupportedException(Type type)
        {
            ConvertToThrowsNotSupportedForType(type);
        }

        [Fact]
        public void ConvertTo_NullCulture()
        {
            string listSep = CultureInfo.CurrentCulture.TextInfo.ListSeparator;
            Assert.Equal($"1{listSep} 2", Converter.ConvertTo(null, null, new Size(1, 2), typeof(string)));
        }

        [Fact]
        public void CreateInstance_CaseSensitive()
        {
            AssertExtensions.Throws<ArgumentException>(null, () =>
            {
                Converter.CreateInstance(null, new Dictionary<string, object>
                {
                    ["width"] = 1,
                    ["Height"] = 1,
                });
            });
        }

        [Fact]
        public void GetProperties()
        {
            var pt = new Size(1, 2);
            var props = Converter.GetProperties(new Size(3, 4));
            Assert.Equal(2, props.Count);
            Assert.Equal(1, props["Width"].GetValue(pt));
            Assert.Equal(2, props["Height"].GetValue(pt));

            props = Converter.GetProperties(null, new Size(3, 4));
            Assert.Equal(2, props.Count);
            Assert.Equal(1, props["Width"].GetValue(pt));
            Assert.Equal(2, props["Height"].GetValue(pt));

            props = Converter.GetProperties(null, new Size(3, 4), null);
            Assert.Equal(3, props.Count);
            Assert.Equal(1, props["Width"].GetValue(pt));
            Assert.Equal(2, props["Height"].GetValue(pt));
            Assert.Equal((object)false, props["IsEmpty"].GetValue(pt));

            props = Converter.GetProperties(null, new Size(3, 4), new Attribute[0]);
            Assert.Equal(3, props.Count);
            Assert.Equal(1, props["Width"].GetValue(pt));
            Assert.Equal(2, props["Height"].GetValue(pt));
            Assert.Equal((object)false, props["IsEmpty"].GetValue(pt));

            // Pick an attribute that cannot be applied to properties to make sure everything gets filtered
            props = Converter.GetProperties(null, new Size(3, 4), new Attribute[] { new System.Reflection.AssemblyCopyrightAttribute("")});
            Assert.Equal(0, props.Count);
        }

        [Theory]
        [MemberData(nameof(SizeData))]
        public void ConvertFromInvariantString(int width, int height)
        {
            var point = (Size)Converter.ConvertFromInvariantString($"{width}, {height}");
            Assert.Equal(width, point.Width);
            Assert.Equal(height, point.Height);
        }

        [Fact]
        public void ConvertFromInvariantString_ArgumentException()
        {
            ConvertFromInvariantStringThrowsArgumentException("1");
        }

        [Fact]
        public void ConvertFromInvariantString_FormatException()
        {
            ConvertFromInvariantStringThrowsFormatInnerException("hello, hello");
        }

        [Theory]
        [MemberData(nameof(SizeData))]
        public void ConvertFromString(int width, int height)
        {
            var point =
                (Size)Converter.ConvertFromString(string.Format("{0}{2} {1}", width, height,
                    CultureInfo.CurrentCulture.TextInfo.ListSeparator));
            Assert.Equal(width, point.Width);
            Assert.Equal(height, point.Height);
        }

        [Fact]
        public void ConvertFromString_ArgumentException()
        {
            ConvertFromStringThrowsArgumentException("1");
        }

        [Fact]
        public void ConvertFromString_FormatException()
        {
            var sep = CultureInfo.CurrentCulture.TextInfo.ListSeparator;
            ConvertFromStringThrowsFormatInnerException($"hello{sep} hello");
        }

        [Theory]
        [MemberData(nameof(SizeData))]
        public void ConvertToInvariantString(int width, int height)
        {
            var str = Converter.ConvertToInvariantString(new Size(width, height));
            Assert.Equal($"{width}, {height}", str);
        }

        [Theory]
        [MemberData(nameof(SizeData))]
        public void ConvertToString(int width, int height)
        {
            var str = Converter.ConvertToString(new Size(width, height));
            Assert.Equal(string.Format("{0}{2} {1}", width, height, CultureInfo.CurrentCulture.TextInfo.ListSeparator), str);
        }
    }
}
