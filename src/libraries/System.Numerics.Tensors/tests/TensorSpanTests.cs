﻿// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

using System.Buffers;
using System.Collections.Generic;
using System.Linq;
using System.Linq.Expressions;
using System.Runtime.InteropServices;
using Xunit;

namespace System.Numerics.Tensors.Tests
{
    public class TensorSpanTests
    {
        #region TensorPrimitivesForwardsTests
        private void FillTensor<T>(Span<T> span)
            where T : INumberBase<T>
        {
            for (int i = 0; i < span.Length; i++)
            {
                span[i] = T.CreateChecked((Random.Shared.NextSingle() * 100) - 50);
            }
        }

        private static nint CalculateTotalLength(ReadOnlySpan<nint> lengths)
        {
            if (lengths.IsEmpty)
                return 0;
            nint totalLength = 1;
            for (int i = 0; i < lengths.Length; i++)
            {
                totalLength *= lengths[i];
            }

            return totalLength;
        }

        public delegate TOut TensorPrimitivesSpanInSpanOut<TIn, TOut>(TIn input);
        public delegate ref readonly TensorSpan<TOut> TensorSpanInSpanOut<TIn, TOut>(scoped in ReadOnlyTensorSpan<TIn> input, in TensorSpan<TOut> destination);

        public static IEnumerable<object[]> SpanInSpanOutData()
        {
            yield return Create<float, float>(float.Abs, Tensor.Abs);
            yield return Create<float, float>(float.Acos, Tensor.Acos);
            yield return Create<float, float>(float.Acosh, Tensor.Acosh);
            yield return Create<float, float>(float.AcosPi, Tensor.AcosPi);
            yield return Create<float, float>(float.Asin, Tensor.Asin);
            yield return Create<float, float>(float.Asinh, Tensor.Asinh);
            yield return Create<float, float>(float.AsinPi, Tensor.AsinPi);
            yield return Create<float, float>(float.Atan, Tensor.Atan);
            yield return Create<float, float>(float.Atanh, Tensor.Atanh);
            yield return Create<float, float>(float.AtanPi, Tensor.AtanPi);
            yield return Create<float, float>(float.Cbrt, Tensor.Cbrt);
            yield return Create<float, float>(float.Ceiling, Tensor.Ceiling);
            yield return Create<float, float>(float.Cos, Tensor.Cos);
            yield return Create<float, float>(float.Cosh, Tensor.Cosh);
            yield return Create<float, float>(float.CosPi, Tensor.CosPi);
            yield return Create<float, float>(float.DegreesToRadians, Tensor.DegreesToRadians);
            yield return Create<float, float>(float.Exp, Tensor.Exp);
            yield return Create<float, float>(float.Exp10, Tensor.Exp10);
            yield return Create<float, float>(float.Exp10M1, Tensor.Exp10M1);
            yield return Create<float, float>(float.Exp2, Tensor.Exp2);
            yield return Create<float, float>(float.Exp2M1, Tensor.Exp2M1);
            yield return Create<float, float>(float.ExpM1, Tensor.ExpM1);
            yield return Create<float, float>(float.Floor, Tensor.Floor);
            yield return Create<int, int>(int.LeadingZeroCount, Tensor.LeadingZeroCount);
            yield return Create<float, float>(float.Log, Tensor.Log);
            yield return Create<float, float>(float.Log10, Tensor.Log10);
            yield return Create<float, float>(float.Log10P1, Tensor.Log10P1);
            yield return Create<float, float>(float.Log2, Tensor.Log2);
            yield return Create<float, float>(float.Log2P1, Tensor.Log2P1);
            yield return Create<float, float>(float.LogP1, Tensor.LogP1);
            yield return Create<float, float>(x => -x, Tensor.Negate);
            yield return Create<int, int>(x => ~x, Tensor.OnesComplement);
            yield return Create<int, int>(int.PopCount, Tensor.PopCount);
            yield return Create<float, float>(float.RadiansToDegrees, Tensor.RadiansToDegrees);
            yield return Create<float, float>(f => 1 / f, Tensor.Reciprocal);
            yield return Create<float, float>(float.Round, Tensor.Round);
            //yield return Create<float, float>(float.Sigmoid, Tensor.Sigmoid);
            yield return Create<float, float>(float.Sin, Tensor.Sin);
            yield return Create<float, float>(float.Sinh, Tensor.Sinh);
            yield return Create<float, float>(float.SinPi, Tensor.SinPi);
            //yield return Create<float, float>(float.SoftMax, Tensor.SoftMax);
            yield return Create<float, float>(float.Sqrt, Tensor.Sqrt);
            yield return Create<float, float>(float.Tan, Tensor.Tan);
            yield return Create<float, float>(float.Tanh, Tensor.Tanh);
            yield return Create<float, float>(float.TanPi, Tensor.TanPi);
            yield return Create<float, float>(float.Truncate, Tensor.Truncate);
            yield return Create<float, int>(float.ILogB, Tensor.ILogB);
            yield return Create<float, int>(x => Expression.Lambda<Func<int>>(Expression.ConvertChecked(Expression.Constant(x), typeof(int))).Compile()(), Tensor.ConvertChecked);
            yield return Create<float, int>(x => (int)x, Tensor.ConvertSaturating);
            yield return Create<float, int>(x => (int)MathF.Truncate(x), Tensor.ConvertTruncating);

            static object[] Create<TIn, TOut>(TensorPrimitivesSpanInSpanOut<TIn, TOut> tensorPrimitivesMethod, TensorSpanInSpanOut<TIn, TOut> tensorOperation)
                => new object[] { tensorPrimitivesMethod, tensorOperation };
        }

        [Theory, MemberData(nameof(SpanInSpanOutData))]
        public void TensorExtensionsSpanInSpanOut<TIn, TOut>(TensorPrimitivesSpanInSpanOut<TIn, TOut> tensorPrimitivesOperation, TensorSpanInSpanOut<TIn, TOut> tensorOperation)
            where TIn : INumberBase<TIn>
            where TOut: INumber<TOut>
        {
            Assert.All(Helpers.TensorShapes, (tensorLength, index) =>
            {
                nint length = CalculateTotalLength(tensorLength);

                TIn[] data = new TIn[length];
                TOut[] data2 = new TOut[length];

                FillTensor<TIn>(data);
                TensorSpan<TIn> x = Tensor.Create<TIn>(data, tensorLength, []);
                TensorSpan<TOut> destination = Tensor.Create<TOut>(data2, tensorLength, []);
                TensorSpan<TOut> tensorResults = tensorOperation(x, destination);

                Assert.Equal(tensorLength, tensorResults.Lengths);
                nint[] startingIndex = new nint[tensorLength.Length];

                // the "Return" value
                ReadOnlySpan<TOut> span = MemoryMarshal.CreateSpan(ref tensorResults.GetPinnableReference(), (int)length);

                for (int i = 0; i < data.Length; i++)
                {
                    Assert.Equal(tensorPrimitivesOperation(data[i]), span[i]);
                }

                // Now test if the source is sliced to be smaller then the destination that the destination is also sliced
                // to the correct size.
                NRange[] sliceLengths = Helpers.TensorSliceShapes[index].Select(i => new NRange(0, i)).ToArray();
                nint sliceFlattenedLength = CalculateTotalLength(Helpers.TensorSliceShapes[index]);
                x = x.Slice(sliceLengths);
                TIn[] sliceData = new TIn[sliceFlattenedLength];

                // Now test if the source and destination are sliced (so neither is continuous) it works correctly.
                destination = destination.Slice(sliceLengths);
                x.FlattenTo(sliceData);

                tensorResults = tensorOperation(x, destination);

                Assert.Equal(Helpers.TensorSliceShapes[index], tensorResults.Lengths);

                TensorSpan<TOut>.Enumerator tensorResultsEnum = tensorResults.GetEnumerator();
                bool tensorResultsEnumMove;
                for (int i = 0; i < sliceData.Length; i++)
                {
                    tensorResultsEnumMove = tensorResultsEnum.MoveNext();

                    Assert.True(tensorResultsEnumMove);
                    Assert.Equal(tensorPrimitivesOperation(sliceData[i]), tensorResultsEnum.Current);
                }
            });
        }

        public delegate T TensorPrimitivesSpanInTOut<T>(ReadOnlySpan<T> input);
        public delegate T TensorSpanInTOut<T>(scoped in ReadOnlyTensorSpan<T> input);
        public static IEnumerable<object[]> SpanInFloatOutData()
        {
            yield return Create<float>(TensorPrimitives.Max, Tensor.Max);
            yield return Create<float>(TensorPrimitives.MaxMagnitude, Tensor.MaxMagnitude);
            yield return Create<float>(TensorPrimitives.MaxNumber, Tensor.MaxNumber);
            yield return Create<float>(TensorPrimitives.Min, Tensor.Min);
            yield return Create<float>(TensorPrimitives.MinMagnitude, Tensor.MinMagnitude);
            yield return Create<float>(TensorPrimitives.MinNumber, Tensor.MinNumber);
            yield return Create<float>(x =>
            {
                float sum = 0;
                for (int i = 0; i < x.Length; i++)
                {
                    sum += x[i] * x[i];
                }
                return float.Sqrt(sum);
            }, Tensor.Norm);
            yield return Create<float>(x =>
            {
                float sum = 1;
                for (int i = 0; i < x.Length; i++)
                {
                    sum *= x[i];
                }
                return sum;
            }, Tensor.Product);
            yield return Create<float>(x =>
            {
                float sum = 0;
                for (int i = 0; i < x.Length; i++)
                {
                    sum += x[i];
                }
                return sum;
            }, Tensor.Sum);

            static object[] Create<T>(TensorPrimitivesSpanInTOut<T> tensorPrimitivesMethod, TensorSpanInTOut<T> tensorOperation)
                => new object[] { tensorPrimitivesMethod, tensorOperation };
        }

        [Theory, MemberData(nameof(SpanInFloatOutData))]
        public void TensorExtensionsSpanInTOut<T>(TensorPrimitivesSpanInTOut<T> tensorPrimitivesOperation, TensorSpanInTOut<T> tensorOperation)
            where T : INumberBase<T>
        {
            Assert.All(Helpers.TensorShapes, (tensorLength, index) =>
            {
                nint length = CalculateTotalLength(tensorLength);
                T[] data = new T[length];

                FillTensor<T>(data);
                Tensor<T> tensor = Tensor.Create<T>(data, tensorLength, []);
                T expectedOutput = tensorPrimitivesOperation((ReadOnlySpan<T>)data);
                T results = tensorOperation(tensor);

                Assert.Equal(expectedOutput, results);

                // Now test if the source is sliced to be non contiguous that it still gives expected result.
                NRange[] sliceLengths = Helpers.TensorSliceShapes[index].Select(i => new NRange(0, i)).ToArray();
                nint sliceFlattenedLength = CalculateTotalLength(Helpers.TensorSliceShapes[index]);
                tensor = tensor.Slice(sliceLengths);
                T[] sliceData = new T[sliceFlattenedLength];
                tensor.FlattenTo(sliceData);

                IEnumerator<T> enumerator = tensor.GetEnumerator();
                bool cont = enumerator.MoveNext();
                int i = 0;
                Assert.True(tensor.SequenceEqual(sliceData));
                while (cont)
                {
                    Assert.Equal(sliceData[i++], enumerator.Current);
                    cont = enumerator.MoveNext();
                }

                expectedOutput = tensorPrimitivesOperation((ReadOnlySpan<T>)sliceData);
                results = tensorOperation(tensor);

                Assert.Equal(expectedOutput, results);
            });
        }

        public delegate T TensorPrimitivesTwoSpanInSpanOut<T>(T input, T inputTwo);
        public delegate ref readonly TensorSpan<T> TensorTwoSpanInSpanOut<T>(scoped in ReadOnlyTensorSpan<T> input, scoped in ReadOnlyTensorSpan<T> inputTwo, in TensorSpan<T> destination);
        public delegate ref readonly TensorSpan<T> TensorTwoSpanInSpanOutInPlace<T>(in TensorSpan<T> input, scoped in ReadOnlyTensorSpan<T> inputTwo);
        public static IEnumerable<object[]> TwoSpanInSpanOutData()
        {
            yield return Create<float>((x, y) => x + y, Tensor.Add);
            yield return Create<float>((x, y) => float.Atan2(x, y), Tensor.Atan2);
            yield return Create<float>((x, y) => float.Atan2Pi(x, y), Tensor.Atan2Pi);
            yield return Create<float>((x, y) => float.CopySign(x, y), Tensor.CopySign);
            yield return Create<float>((x, y) => x / y, Tensor.Divide);
            yield return Create<float>((x, y) => float.Hypot(x, y), Tensor.Hypot);
            yield return Create<float>((x, y) => float.Ieee754Remainder(x, y), Tensor.Ieee754Remainder);
            yield return Create<float>((x, y) => x * y, Tensor.Multiply);
            yield return Create<float>((x, y) => float.Pow(x, y), Tensor.Pow);
            yield return Create<float>((x, y) => x - y, Tensor.Subtract);

            static object[] Create<T>(TensorPrimitivesTwoSpanInSpanOut<T> tensorPrimitivesMethod, TensorTwoSpanInSpanOut<T> tensorOperation)
                => new object[] { tensorPrimitivesMethod, tensorOperation };
        }

        [Theory, MemberData(nameof(TwoSpanInSpanOutData))]
        public void TensorExtensionsTwoSpanInSpanOut<T>(TensorPrimitivesTwoSpanInSpanOut<T> tensorPrimitivesOperation, TensorTwoSpanInSpanOut<T> tensorOperation)
            where T : INumberBase<T>
        {
            Assert.All(Helpers.TensorShapes, (tensorLengths, index) =>
            {
                nint length = CalculateTotalLength(tensorLengths);
                T[] data1 = new T[length];
                T[] data2 = new T[length];
                T[] destData = new T[length];
                T[] expectedOutput = new T[length];

                FillTensor<T>(data1);
                FillTensor<T>(data2);


                // First test when everything is exact sizes
                TensorSpan<T> x = Tensor.Create<T>(data1, tensorLengths, []);
                TensorSpan<T> y = Tensor.Create<T>(data2, tensorLengths, []);
                TensorSpan<T> destination = Tensor.Create<T>(destData, tensorLengths, []);
                TensorSpan<T> results = tensorOperation(x, y, destination);

                Assert.Equal(tensorLengths, results.Lengths);
                nint[] startingIndex = new nint[tensorLengths.Length];
                // the "Return" value
                ReadOnlySpan<T> span = MemoryMarshal.CreateSpan(ref results.GetPinnableReference(), (int)length);

                for (int i = 0; i < data1.Length; i++)
                {
                    Assert.Equal(tensorPrimitivesOperation(data1[i], data2[i]), span[i]);
                }


                // Now test if the first source is sliced to be smaller than the second (but is broadcast compatible) that broadcasting happens).
                int rowLength = (int)Helpers.TensorSliceShapesForBroadcast[index][^1];

                NRange[] sliceLengths = Helpers.TensorSliceShapesForBroadcast[index].Select(i => new NRange(0, i)).ToArray();
                nint sliceFlattenedLength = CalculateTotalLength(Helpers.TensorSliceShapesForBroadcast[index]);
                //destination = destination.Slice(sliceLengths);
                x.Slice(sliceLengths).BroadcastTo(x);
                x.FlattenTo(data1);

                results = tensorOperation(x.Slice(sliceLengths), y, destination);

                // results lengths will still be the original tensorLength
                Assert.Equal(tensorLengths, results.Lengths);

                TensorSpan<T>.Enumerator tensorResultsEnum = results.GetEnumerator();
                TensorSpan<T>.Enumerator xEnum = x.GetEnumerator();
                TensorSpan<T>.Enumerator yEnum = y.GetEnumerator();
                bool tensorResultsEnumMove;

                for (int i = 0; i < results.FlattenedLength; i++)
                {
                    tensorResultsEnumMove = tensorResultsEnum.MoveNext();
                    xEnum.MoveNext();
                    yEnum.MoveNext();
                    Assert.True(tensorResultsEnumMove);

                    Assert.Equal(tensorPrimitivesOperation(xEnum.Current, yEnum.Current), tensorResultsEnum.Current);
                }

                // Now test if the second source is sliced to be smaller than the first (but is broadcast compatible) that broadcasting happens).
                y.Slice(sliceLengths).BroadcastTo(y);
                y.FlattenTo(data2);

                results = tensorOperation(x, y.Slice(sliceLengths), destination);

                // results lengths will still be the original tensorLength
                Assert.Equal(tensorLengths, results.Lengths);

                tensorResultsEnum = results.GetEnumerator();
                xEnum = x.GetEnumerator();
                yEnum = y.GetEnumerator();
                for (int i = 0; i < results.FlattenedLength; i++)
                {
                    tensorResultsEnumMove = tensorResultsEnum.MoveNext();
                    xEnum.MoveNext();
                    yEnum.MoveNext();
                    Assert.True(tensorResultsEnumMove);
                    Assert.Equal(tensorPrimitivesOperation(xEnum.Current, yEnum.Current), tensorResultsEnum.Current);
                }
            });
        }

        public delegate T TensorPrimitivesTwoSpanInTOut<T>(ReadOnlySpan<T> input, ReadOnlySpan<T> inputTwo);
        public delegate T TensorTwoSpanInTOut<T>(in ReadOnlyTensorSpan<T> input, in ReadOnlyTensorSpan<T> inputTwo);
        public static IEnumerable<object[]> TwoSpanInFloatOutData()
        {
            yield return Create<float>(TensorPrimitives.Distance, Tensor.Distance);
            yield return Create<float>(TensorPrimitives.Dot, Tensor.Dot);

            static object[] Create<T>(TensorPrimitivesTwoSpanInTOut<T> tensorPrimitivesMethod, TensorTwoSpanInTOut<T> tensorOperation)
                => new object[] { tensorPrimitivesMethod, tensorOperation };
        }

        [ActiveIssue("https://github.com/dotnet/runtime/issues/107254")]
        [Theory, MemberData(nameof(TwoSpanInFloatOutData))]
        public void TensorExtensionsTwoSpanInFloatOut<T>(TensorPrimitivesTwoSpanInTOut<T> tensorPrimitivesOperation, TensorTwoSpanInTOut<T> tensorOperation)
            where T : INumberBase<T>
        {
            Assert.All(Helpers.TensorShapes, (tensorLength, index) =>
            {
                nint length = CalculateTotalLength(tensorLength);
                T[] data1 = new T[length];
                T[] data2 = new T[length];
                T[] broadcastData1 = new T[length];
                T[] broadcastData2 = new T[length];

                FillTensor<T>(data1);
                FillTensor<T>(data2);
                TensorSpan<T> x = Tensor.Create<T>(data1, tensorLength, []);
                TensorSpan<T> y = Tensor.Create<T>(data2, tensorLength, []);
                T expectedOutput = tensorPrimitivesOperation((ReadOnlySpan<T>)data1, data2);
                T results = tensorOperation(x, y);

                Assert.Equal(expectedOutput, results);

                // Now test if the first source is sliced to be non contiguous that it still gives expected result.
                NRange[] sliceLengths = Helpers.TensorSliceShapesForBroadcast[index].Select(i => new NRange(0, i)).ToArray();
                TensorSpan<T> broadcastX = Tensor.Create<T>(broadcastData1, tensorLength, []);
                x.Slice(sliceLengths).BroadcastTo(broadcastX);
                TensorSpan<T>.Enumerator enumerator = broadcastX.GetEnumerator();
                bool cont = enumerator.MoveNext();
                int i = 0;
                while (cont)
                {
                    Assert.Equal(broadcastData1[i++], enumerator.Current);
                    cont = enumerator.MoveNext();
                }

                expectedOutput = tensorPrimitivesOperation((ReadOnlySpan<T>)broadcastData1, data2);
                results = tensorOperation(x.Slice(sliceLengths), y);

                Assert.Equal(expectedOutput, results);

                // Now test if the second source is sliced to be non contiguous that it still gives expected result.

                TensorSpan<T> broadcastY = Tensor.Create<T>(broadcastData2, tensorLength, []);
                y.Slice(sliceLengths).BroadcastTo(broadcastY);

                enumerator = broadcastY.GetEnumerator();
                cont = enumerator.MoveNext();
                i = 0;
                while (cont)
                {
                    Assert.Equal(broadcastData2[i++], enumerator.Current);
                    cont = enumerator.MoveNext();
                }

                expectedOutput = tensorPrimitivesOperation((ReadOnlySpan<T>)data1, broadcastData2);
                results = tensorOperation(x, y.Slice(sliceLengths));

                Assert.Equal(expectedOutput, results);
            });
        }

        #endregion

        [Fact]
        public static unsafe void TensorSpanSetSliceTests()
        {
            // Cannot reshape if memory is non-contiguous or non-dense
            Assert.Throws<ArgumentException>(() =>
            {
                var ab = new TensorSpan<double>(array: [0d, 1, 2, 3, 0d, 1, 2, 3]);  // [0, 1, 2, 3]
                var b = ab.Reshape(lengths: new IntPtr[] { 2, 2, 2 });  // [[0, 1], [2, 3]]
                var c = b.Slice(new NRange[] { 1.., 1..2, ..1 });  // [[0], [2]]
                c.Reshape(lengths: new IntPtr[] { 1, 2, 1 });
            });

            // Make sure even if the Lengths are the same that the underlying memory has to be the same.
            Assert.Throws<ArgumentException>(() =>
            {
                var ar = new double[1];
                var a = new TensorSpan<double>(ar.AsSpan()[..1], new IntPtr[] { 2 }, new IntPtr[] { 0 });
                a.SetSlice(new TensorSpan<double>(new double[] { 1, 3 }), new NRange[] { ..2 });
            });

            // Make sure that slice range and the values are the same length
            var ar = new double[4];
            var a = new TensorSpan<double>(ar, new IntPtr[] { 2, 2 }, default);

            a.SetSlice(new TensorSpan<double>(new double[] { 1, 3 }), new NRange[] { ..1, .. });
            Assert.Equal(1, a[0, 0]);
            Assert.Equal(3, a[0, 1]);
            Assert.Equal(0, a[1, 0]);
            Assert.Equal(0, a[1, 1]);

            // Make sure we can use a stride of 0.
            a.SetSlice(new TensorSpan<double>(new double[] { -1 }, [2], [0]), new NRange[] { 1.., .. });
            Assert.Equal(1, a[0, 0]);
            Assert.Equal(3, a[0, 1]);
            Assert.Equal(-1, a[1, 0]);
            Assert.Equal(-1, a[1, 1]);

            // Make sure we can use a multi dimensional span with multiple 0 strides
            a.SetSlice(new TensorSpan<double>(new double[] { -10 }, [2, 2], [0, 0]));
            Assert.Equal(-10, a[0, 0]);
            Assert.Equal(-10, a[0, 1]);
            Assert.Equal(-10, a[1, 0]);
            Assert.Equal(-10, a[1, 1]);

            // Make sure if the slice is broadcastable to the correct size you don't need to set a size for SetSlice
            a.SetSlice(new TensorSpan<double>(new double[] { -20 }, [1], [0]));
            Assert.Equal(-20, a[0, 0]);
            Assert.Equal(-20, a[0, 1]);
            Assert.Equal(-20, a[1, 0]);
            Assert.Equal(-20, a[1, 1]);

            //Assert.Throws
        }

        [Fact]
        public static void TensorSpanSystemArrayConstructorTests()
        {
            // When using System.Array constructor make sure the type of the array matches T[]
            Assert.Throws<ArrayTypeMismatchException>(() => new TensorSpan<double>(array: new[] { 1 }));

            string[] stringArray = { "a", "b", "c" };
            Assert.Throws<ArrayTypeMismatchException>(() => new TensorSpan<object>(array: stringArray));

            // Make sure basic T[,] constructor works
            int[,] a = new int[,] { { 91, 92, -93, 94 } };
            scoped TensorSpan<int> spanInt = new TensorSpan<int>(a);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(4, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[0, 2]);
            Assert.Equal(94, spanInt[0, 3]);

            // Make sure null works
            // Should be a tensor with 0 elements and Rank 0 and no strides or lengths
            int[,] n = null;
            spanInt = new TensorSpan<int>(n);
            Assert.Equal(0, spanInt.Rank);
            Assert.Equal(0, spanInt.Lengths.Length);
            Assert.Equal(0, spanInt.Strides.Length);

            // Make sure empty array works
            // Should be a Tensor with 0 elements but Rank 2 with dimension 0 length 0
            int[,] b = { { } };
            spanInt = new TensorSpan<int>(b);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(0, spanInt.Lengths[1]);
            Assert.Equal(0, spanInt.FlattenedLength);
            Assert.Equal(0, spanInt.Strides[0]);
            Assert.Equal(0, spanInt.Strides[1]);
            // Make sure it still throws on index 0, 0
            Assert.Throws<IndexOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(b);
                var x = spanInt[0, 0];
            });

            // Make sure 2D array works
            spanInt = new TensorSpan<int>(a, (int[])[0, 0], [2, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure can use only some of the array
            spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Throws<IndexOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], default);
                var x = spanInt[1, 1];
            });

            Assert.Throws<IndexOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], default);
                var x = spanInt[0, -1];
            });

            Assert.Throws<IndexOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], default);
                var x = spanInt[-1, 0];
            });

            Assert.Throws<IndexOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], default);
                var x = spanInt[1, 0];
            });

            // Make sure Index offset works correctly
            spanInt = new TensorSpan<int>(a, (int[])[0, 1], [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(92, spanInt[0, 0]);
            Assert.Equal(-93, spanInt[0, 1]);

            // Make sure Index offset works correctly
            spanInt = new TensorSpan<int>(a, (int[])[0, 2], [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(-93, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);

            // Make sure 2D array works with strides of all 0 and initial offset to loop over last element again
            spanInt = new TensorSpan<int>(a, (int[])[0, 3], [2, 2], [0, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(94, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(94, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure we catch that there aren't enough elements in the array for the lengths
            Assert.Throws<ArgumentOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 3], [1, 2], default);
            });

            // Make sure 2D array works with basic strides
            spanInt = new TensorSpan<int>(a, (int[])[0, 0], [2, 2], [2, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure 2D array works with stride of 0 to loop over first 2 elements again
            spanInt = new TensorSpan<int>(a, (int[])[0, 0], [2, 2], [0, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(91, spanInt[1, 0]);
            Assert.Equal(92, spanInt[1, 1]);

            // Make sure 2D array works with stride of 0 and initial offset to loop over last 2 elements again
            spanInt = new TensorSpan<int>(a, (int[])[0, 2], [2, 2], [0, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(-93, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure strides can't be negative
            Assert.Throws<ArgumentOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], [-1, 0]);
            });
            Assert.Throws<ArgumentOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, 2], [0, -1]);
            });

            // Make sure lengths can't be negative
            Assert.Throws<ArgumentOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [-1, 2], []);
            });
            Assert.Throws<ArgumentOutOfRangeException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[0, 0], [1, -2], []);
            });

            // Make sure 2D array works with strides to hit element 0,0,2,2
            spanInt = new TensorSpan<int>(a, (int[])[], [2, 2], [2, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(91, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(-93, spanInt[1, 1]);

            // Make sure you can't overlap elements using strides
            Assert.Throws<ArgumentException>(() =>
            {
                var spanInt = new TensorSpan<int>(a, (int[])[], [2, 2], [1, 1]);
            });

            a = new int[,] { { 91, 92 }, { -93, 94 } };
            spanInt = new TensorSpan<int>(a, (int[])[1, 1], [2, 2], [0, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(94, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(94, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // // TODO: Make sure it works with NIndex
            // spanInt = new TensorSpan<int>(a, [0, 0], (NIndex[])[1, 1], [2, 2]);
            // Assert.Equal(2, spanInt.Rank);
            // Assert.Equal(2, spanInt.Lengths[0]);
            // Assert.Equal(2, spanInt.Lengths[1]);
            // Assert.Equal(94, spanInt[0, 0]);
            // Assert.Equal(94, spanInt[0, 1]);
            // Assert.Equal(94, spanInt[1, 0]);
            // Assert.Equal(94, spanInt[1, 1]);

            // // TODO: Make sure it works with NIndex
            // spanInt = new TensorSpan<int>(a, [0, 0], (NIndex[])[^1, ^1], [2, 2]);
            // Assert.Equal(2, spanInt.Rank);
            // Assert.Equal(2, spanInt.Lengths[0]);
            // Assert.Equal(2, spanInt.Lengths[1]);
            // Assert.Equal(94, spanInt[0, 0]);
            // Assert.Equal(94, spanInt[0, 1]);
            // Assert.Equal(94, spanInt[1, 0]);
            // Assert.Equal(94, spanInt[1, 1]);
        }

        [Fact]
        public static void TensorSpanArrayConstructorTests()
        {
            // Make sure exception is thrown if lengths and strides would let you go past the end of the array
            Assert.Throws<ArgumentOutOfRangeException>(() => new TensorSpan<double>(new double[0], lengths: new IntPtr[] { 2 }, strides: new IntPtr[] { 1 }));
            Assert.Throws<ArgumentOutOfRangeException>(() => new TensorSpan<double>(new double[1], lengths: new IntPtr[] { 2 }, strides: new IntPtr[] { 1 }));
            Assert.Throws<ArgumentOutOfRangeException>(() => new TensorSpan<double>(new double[2], lengths: new IntPtr[] { 2 }, strides: new IntPtr[] { 2 }));

            // Make sure basic T[] constructor works
            int[] a = { 91, 92, -93, 94 };
            scoped TensorSpan<int> spanInt = new TensorSpan<int>(a);
            Assert.Equal(1, spanInt.Rank);
            Assert.Equal(4, spanInt.Lengths[0]);
            Assert.Equal(91, spanInt[0]);
            Assert.Equal(92, spanInt[1]);
            Assert.Equal(-93, spanInt[2]);
            Assert.Equal(94, spanInt[3]);

            // Make sure null works
            // Should be a tensor with 0 elements and Rank 0 and no strides or lengths
            spanInt = new TensorSpan<int>(null);
            Assert.Equal(0, spanInt.Rank);
            Assert.Equal(0, spanInt.Lengths.Length);
            Assert.Equal(0, spanInt.Strides.Length);

            // Make sure empty array works
            // Should be a Tensor with 0 elements but Rank 1 with dimension 0 length 0
            int[] b = { };
            spanInt = new TensorSpan<int>(b);
            Assert.Equal(1, spanInt.Rank);
            Assert.Equal(0, spanInt.Lengths[0]);
            Assert.Equal(0, spanInt.FlattenedLength);
            Assert.Equal(1, spanInt.Strides[0]);
            // Make sure it still throws on index 0
            Assert.Throws<IndexOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(b);
                var x = spanInt[0];
            });

            // Make sure empty array works
            // Should be a Tensor with 0 elements but Rank 1 with dimension 0 length 0
            spanInt = new TensorSpan<int>(b, 0, [], default);
            Assert.Equal(1, spanInt.Rank);
            Assert.Equal(0, spanInt.Lengths[0]);
            Assert.Equal(0, spanInt.FlattenedLength);
            Assert.Equal(0, spanInt.Strides[0]);

            // Make sure 2D array works
            spanInt = new TensorSpan<int>(a, 0, [2, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure can use only some of the array
            spanInt = new TensorSpan<int>(a, 0, [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Throws<IndexOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 0, [1, 2], default);
                var x = spanInt[1, 1];
            });

            Assert.Throws<IndexOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 0, [1, 2], default);
                var x = spanInt[1, 0];
            });

            // Make sure Index offset works correctly
            spanInt = new TensorSpan<int>(a, 1, [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(92, spanInt[0, 0]);
            Assert.Equal(-93, spanInt[0, 1]);

            // Make sure Index offset works correctly
            spanInt = new TensorSpan<int>(a, 2, [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(-93, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);

            // Make sure we catch that there aren't enough elements in the array for the lengths
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 3, [1, 2], default);
            });

            // Make sure 2D array works with basic strides
            spanInt = new TensorSpan<int>(a, 0, [2, 2], [2, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure 2D array works with stride of 0 to loop over first 2 elements again
            spanInt = new TensorSpan<int>(a, 0, [2, 2], [0, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(91, spanInt[1, 0]);
            Assert.Equal(92, spanInt[1, 1]);

            // Make sure 2D array works with stride of 0 and initial offset to loop over last 2 elements again
            spanInt = new TensorSpan<int>(a, 2, [2, 2], [0, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(-93, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure 2D array works with strides of all 0 and initial offset to loop over last element again
            spanInt = new TensorSpan<int>(a, 3, [2, 2], [0, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(94, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(94, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure strides can't be negative
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 3, [1, 2], [-1, 0]);
            });
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 3, [1, 2], [0, -1]);
            });

            // Make sure lengths can't be negative
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 3, [-1, 2], []);
            });
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                var spanInt = new TensorSpan<int>(a, 3, [1, -2], []);
            });

            // Make sure 2D array works with strides to hit element 0,0,2,2
            spanInt = new TensorSpan<int>(a, 0, [2, 2], [2, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(91, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(-93, spanInt[1, 1]);

            // Make sure you can't overlap elements using strides
            Assert.Throws<ArgumentException>(() => {
                var spanInt = new TensorSpan<int>(a, 0, [2, 2], [1, 1]);
            });
        }

        [Fact]
        public static void TensorSpanSpanConstructorTests()
        {
            // Make sure basic T[] constructor works
            Span<int> a = [91, 92, -93, 94];
            scoped TensorSpan<int> spanInt = new TensorSpan<int>(a);
            Assert.Equal(1, spanInt.Rank);
            Assert.Equal(4, spanInt.Lengths[0]);
            Assert.Equal(91, spanInt[0]);
            Assert.Equal(92, spanInt[1]);
            Assert.Equal(-93, spanInt[2]);
            Assert.Equal(94, spanInt[3]);

            // Make sure empty span works
            // Should be a Tensor with 0 elements but Rank 1 with dimension 0 length 0
            Span<int> b = [];
            spanInt = new TensorSpan<int>(b);
            Assert.Equal(0, spanInt.Rank);
            Assert.Equal(0, spanInt.Lengths.Length);
            Assert.Equal(0, spanInt.FlattenedLength);
            Assert.Equal(0, spanInt.Strides.Length);
            // Make sure it still throws on index 0
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                Span<int> b = [];
                var spanInt = new TensorSpan<int>(b);
                var x = spanInt[0];
            });

            // Make sure 2D array works
            spanInt = new TensorSpan<int>(a, [2, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure can use only some of the array
            spanInt = new TensorSpan<int>(a, [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Throws<IndexOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [1, 2], default);
                var x = spanInt[1, 1];
            });

            Assert.Throws<IndexOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [1, 2], default);
                var x = spanInt[1, 0];
            });

            // Make sure Index offset works correctly
            spanInt = new TensorSpan<int>(a.Slice(1), [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(92, spanInt[0, 0]);
            Assert.Equal(-93, spanInt[0, 1]);

            // Make sure Index offset works correctly
            spanInt = new TensorSpan<int>(a.Slice(2), [1, 2], default);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(-93, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);

            // Make sure we catch that there aren't enough elements in the array for the lengths
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a.Slice(3), [1, 2], default);
            });

            // Make sure 2D array works with basic strides
            spanInt = new TensorSpan<int>(a, [2, 2], [2, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure 2D array works with stride of 0 to loop over first 2 elements again
            spanInt = new TensorSpan<int>(a, [2, 2], [0, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(91, spanInt[1, 0]);
            Assert.Equal(92, spanInt[1, 1]);

            // Make sure 2D array works with stride of 0 and initial offset to loop over last 2 elements again
            spanInt = new TensorSpan<int>(a.Slice(2), [2, 2], [0, 1]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(-93, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure 2D array works with strides of all 0 and initial offset to loop over last element again
            spanInt = new TensorSpan<int>(a.Slice(3), [2, 2], [0, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(94, spanInt[0, 0]);
            Assert.Equal(94, spanInt[0, 1]);
            Assert.Equal(94, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            // Make sure strides can't be negative
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [1, 2], [-1, 0]);
            });
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [1, 2], [0, -1]);
            });

            // Make sure lengths can't be negative
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [-1, 2], []);
            });
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [1, -2], []);
            });

            // Make sure 2D array works with strides to hit element 0,0,2,2
            spanInt = new TensorSpan<int>(a, [2, 2], [2, 0]);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(91, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(-93, spanInt[1, 1]);

            // Make sure you can't overlap elements using strides
            Assert.Throws<ArgumentException>(() => {
                Span<int> a = [91, 92, -93, 94];
                var spanInt = new TensorSpan<int>(a, [2, 2], [1, 1]);
            });
        }

        [Fact]
        public static unsafe void TensorSpanPointerConstructorTests()
        {
            // Make sure basic T[] constructor works
            Span<int> a = [91, 92, -93, 94];
            TensorSpan<int> spanInt;
            fixed (int* p = a)
            {
                spanInt = new TensorSpan<int>(p, 4);
                Assert.Equal(1, spanInt.Rank);
                Assert.Equal(4, spanInt.Lengths[0]);
                Assert.Equal(91, spanInt[0]);
                Assert.Equal(92, spanInt[1]);
                Assert.Equal(-93, spanInt[2]);
                Assert.Equal(94, spanInt[3]);
            }

            // Make sure empty span works
            // Should be a Tensor with 0 elements but Rank 1 with dimension 0 length 0
            Span<int> b = [];
            fixed (int* p = b)
            {
                spanInt = new TensorSpan<int>(p, 0);
                Assert.Equal(0, spanInt.Rank);
                Assert.Equal(0, spanInt.Lengths.Length);
                Assert.Equal(0, spanInt.FlattenedLength);
                Assert.Equal(0, spanInt.Strides.Length);
                // Make sure it still throws on index 0
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> b = [];
                    fixed (int* p = b)
                    {
                        var spanInt = new TensorSpan<int>(p, 0);
                        var x = spanInt[0];
                    }
                });
            }

            // Make sure 2D array works
            fixed (int* p = a)
            {
                spanInt = new TensorSpan<int>(p, 4, [2, 2], default);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(2, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(91, spanInt[0, 0]);
                Assert.Equal(92, spanInt[0, 1]);
                Assert.Equal(-93, spanInt[1, 0]);
                Assert.Equal(94, spanInt[1, 1]);

                // Make sure can use only some of the array
                spanInt = new TensorSpan<int>(p, 4, [1, 2], default);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(1, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(91, spanInt[0, 0]);
                Assert.Equal(92, spanInt[0, 1]);
                Assert.Throws<IndexOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [1, 2], default);
                        var x = spanInt[1, 1];
                    }
                });

                Assert.Throws<IndexOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [1, 2], default);
                        var x = spanInt[1, 0];
                    }
                });

                // Make sure Index offset works correctly
                spanInt = new TensorSpan<int>(p + 1, 3, [1, 2], default);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(1, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(92, spanInt[0, 0]);
                Assert.Equal(-93, spanInt[0, 1]);

                // Make sure Index offset works correctly
                spanInt = new TensorSpan<int>(p + 2, 2, [1, 2], default);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(1, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(-93, spanInt[0, 0]);
                Assert.Equal(94, spanInt[0, 1]);

                // Make sure we catch that there aren't enough elements in the array for the lengths
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p + 3, 1, [1, 2], default);
                    }
                });

                // Make sure 2D array works with basic strides
                spanInt = new TensorSpan<int>(p, 4, [2, 2], [2, 1]);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(2, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(91, spanInt[0, 0]);
                Assert.Equal(92, spanInt[0, 1]);
                Assert.Equal(-93, spanInt[1, 0]);
                Assert.Equal(94, spanInt[1, 1]);

                // Make sure 2D array works with stride of 0 to loop over first 2 elements again
                spanInt = new TensorSpan<int>(p, 4, [2, 2], [0, 1]);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(2, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(91, spanInt[0, 0]);
                Assert.Equal(92, spanInt[0, 1]);
                Assert.Equal(91, spanInt[1, 0]);
                Assert.Equal(92, spanInt[1, 1]);

                // Make sure 2D array works with stride of 0 and initial offset to loop over last 2 elements again
                spanInt = new TensorSpan<int>(p + 2, 2, [2, 2], [0, 1]);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(2, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(-93, spanInt[0, 0]);
                Assert.Equal(94, spanInt[0, 1]);
                Assert.Equal(-93, spanInt[1, 0]);
                Assert.Equal(94, spanInt[1, 1]);

                // Make sure 2D array works with strides of all 0 and initial offset to loop over last element again
                spanInt = new TensorSpan<int>(p + 3, 1, [2, 2], [0, 0]);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(2, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(94, spanInt[0, 0]);
                Assert.Equal(94, spanInt[0, 1]);
                Assert.Equal(94, spanInt[1, 0]);
                Assert.Equal(94, spanInt[1, 1]);

                // Make sure strides can't be negative
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [1, 2], [-1, 0]);
                    }
                });
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [1, 2], [0, -1]);
                    }
                });

                // Make sure lengths can't be negative
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [-1, 2], []);
                    }
                });
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [1, -2], []);
                    }
                });

                // Make sure can't use negative data length amount
                Assert.Throws<ArgumentOutOfRangeException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, -1, [1, -2], []);
                    }
                });

                // Make sure 2D array works with strides to hit element 0,0,2,2
                spanInt = new TensorSpan<int>(p, 4, [2, 2], [2, 0]);
                Assert.Equal(2, spanInt.Rank);
                Assert.Equal(2, spanInt.Lengths[0]);
                Assert.Equal(2, spanInt.Lengths[1]);
                Assert.Equal(91, spanInt[0, 0]);
                Assert.Equal(91, spanInt[0, 1]);
                Assert.Equal(-93, spanInt[1, 0]);
                Assert.Equal(-93, spanInt[1, 1]);

                // Make sure you can't overlap elements using strides
                Assert.Throws<ArgumentException>(() =>
                {
                    Span<int> a = [91, 92, -93, 94];
                    fixed (int* p = a)
                    {
                        var spanInt = new TensorSpan<int>(p, 4, [2, 2], [1, 1]);
                    }
                });
            }
        }

        [Fact]
        public static void TensorSpanLargeDimensionsTests()
        {
            int[] a = { 91, 92, -93, 94, 95, -96 };
            int[] results = new int[6];
            TensorSpan<int> spanInt = a.AsTensorSpan([1, 1, 1, 1, 1, 6]);
            Assert.Equal(6, spanInt.Rank);

            Assert.Equal(6, spanInt.Lengths.Length);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(1, spanInt.Lengths[1]);
            Assert.Equal(1, spanInt.Lengths[2]);
            Assert.Equal(1, spanInt.Lengths[3]);
            Assert.Equal(1, spanInt.Lengths[4]);
            Assert.Equal(6, spanInt.Lengths[5]);
            Assert.Equal(6, spanInt.Strides.Length);
            Assert.Equal(0, spanInt.Strides[0]);
            Assert.Equal(0, spanInt.Strides[1]);
            Assert.Equal(0, spanInt.Strides[2]);
            Assert.Equal(0, spanInt.Strides[3]);
            Assert.Equal(0, spanInt.Strides[4]);
            Assert.Equal(1, spanInt.Strides[5]);
            Assert.Equal(91, spanInt[0, 0, 0, 0, 0, 0]);
            Assert.Equal(92, spanInt[0, 0, 0, 0, 0, 1]);
            Assert.Equal(-93, spanInt[0, 0, 0, 0, 0, 2]);
            Assert.Equal(94, spanInt[0, 0, 0, 0, 0, 3]);
            Assert.Equal(95, spanInt[0, 0, 0, 0, 0, 4]);
            Assert.Equal(-96, spanInt[0, 0, 0, 0, 0, 5]);
            spanInt.FlattenTo(results);
            Assert.Equal(a, results);

            a = [91, 92, -93, 94, 95, -96, -91, -92, 93, -94, -95, 96];
            results = new int[12];
            spanInt = a.AsTensorSpan([1, 2, 2, 1, 1, 3]);
            Assert.Equal(6, spanInt.Lengths.Length);
            Assert.Equal(1, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(2, spanInt.Lengths[2]);
            Assert.Equal(1, spanInt.Lengths[3]);
            Assert.Equal(1, spanInt.Lengths[4]);
            Assert.Equal(3, spanInt.Lengths[5]);
            Assert.Equal(6, spanInt.Strides.Length);
            Assert.Equal(0, spanInt.Strides[0]);
            Assert.Equal(6, spanInt.Strides[1]);
            Assert.Equal(3, spanInt.Strides[2]);
            Assert.Equal(0, spanInt.Strides[3]);
            Assert.Equal(0, spanInt.Strides[4]);
            Assert.Equal(1, spanInt.Strides[5]);
            Assert.Equal(91, spanInt[0, 0, 0, 0, 0, 0]);
            Assert.Equal(92, spanInt[0, 0, 0, 0, 0, 1]);
            Assert.Equal(-93, spanInt[0, 0, 0, 0, 0, 2]);
            Assert.Equal(94, spanInt[0, 0, 1, 0, 0, 0]);
            Assert.Equal(95, spanInt[0, 0, 1, 0, 0, 1]);
            Assert.Equal(-96, spanInt[0, 0, 1, 0, 0, 2]);
            Assert.Equal(-91, spanInt[0, 1, 0, 0, 0, 0]);
            Assert.Equal(-92, spanInt[0, 1, 0, 0, 0, 1]);
            Assert.Equal(93, spanInt[0, 1, 0, 0, 0, 2]);
            Assert.Equal(-94, spanInt[0, 1, 1, 0, 0, 0]);
            Assert.Equal(-95, spanInt[0, 1, 1, 0, 0, 1]);
            Assert.Equal(96, spanInt[0, 1, 1, 0, 0, 2]);
            spanInt.FlattenTo(results);
            Assert.Equal(a, results);
        }

        [Fact]
        public static void IntArrayAsTensorSpan()
        {
            int[] a = { 91, 92, -93, 94 };
            int[] results = new int[4];
            TensorSpan<int> spanInt = a.AsTensorSpan([4]);
            Assert.Equal(1, spanInt.Rank);

            Assert.Equal(1, spanInt.Lengths.Length);
            Assert.Equal(4, spanInt.Lengths[0]);
            Assert.Equal(1, spanInt.Strides.Length);
            Assert.Equal(1, spanInt.Strides[0]);
            Assert.Equal(91, spanInt[0]);
            Assert.Equal(92, spanInt[1]);
            Assert.Equal(-93, spanInt[2]);
            Assert.Equal(94, spanInt[3]);
            spanInt.FlattenTo(results);
            Assert.Equal(a, results);
            spanInt[0] = 100;
            spanInt[1] = 101;
            spanInt[2] = -102;
            spanInt[3] = 103;

            Assert.Equal(100, spanInt[0]);
            Assert.Equal(101, spanInt[1]);
            Assert.Equal(-102, spanInt[2]);
            Assert.Equal(103, spanInt[3]);

            a[0] = 91;
            a[1] = 92;
            a[2] = -93;
            a[3] = 94;
            spanInt = a.AsTensorSpan([2, 2]);
            spanInt.FlattenTo(results);
            Assert.Equal(a, results);
            Assert.Equal(2, spanInt.Rank);
            Assert.Equal(2, spanInt.Lengths.Length);
            Assert.Equal(2, spanInt.Lengths[0]);
            Assert.Equal(2, spanInt.Lengths[1]);
            Assert.Equal(2, spanInt.Strides.Length);
            Assert.Equal(2, spanInt.Strides[0]);
            Assert.Equal(1, spanInt.Strides[1]);
            Assert.Equal(91, spanInt[0, 0]);
            Assert.Equal(92, spanInt[0, 1]);
            Assert.Equal(-93, spanInt[1, 0]);
            Assert.Equal(94, spanInt[1, 1]);

            spanInt[0, 0] = 100;
            spanInt[0, 1] = 101;
            spanInt[1, 0] = -102;
            spanInt[1, 1] = 103;

            Assert.Equal(100, spanInt[0, 0]);
            Assert.Equal(101, spanInt[0, 1]);
            Assert.Equal(-102, spanInt[1, 0]);
            Assert.Equal(103, spanInt[1, 1]);
        }

        [Fact]
        public static void TensorSpanFillTest()
        {
            int[] a = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            TensorSpan<int> spanInt = a.AsTensorSpan([3, 3]);
            spanInt.Fill(-1);
            var enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(-1, enumerator.Current);
            }

            spanInt.Fill(int.MinValue);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(int.MinValue, enumerator.Current);
            }

            spanInt.Fill(int.MaxValue);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(int.MaxValue, enumerator.Current);
            }

            a = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            spanInt = a.AsTensorSpan([9]);
            spanInt.Fill(-1);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(-1, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 27)];
            spanInt = a.AsTensorSpan([3,3,3]);
            spanInt.Fill(-1);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(-1, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 12)];
            spanInt = a.AsTensorSpan([3, 2, 2]);
            spanInt.Fill(-1);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(-1, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 16)];
            spanInt = a.AsTensorSpan([2,2,2,2]);
            spanInt.Fill(-1);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(-1, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 24)];
            spanInt = a.AsTensorSpan([3, 2, 2, 2]);
            spanInt.Fill(-1);
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(-1, enumerator.Current);
            }
        }

        [Fact]
        public static void TensorSpanClearTest()
        {
            int[] a = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            TensorSpan<int> spanInt = a.AsTensorSpan([3, 3]);

            var slice = spanInt.Slice(0..2, 0..2);
            slice.Clear();
            Assert.Equal(0, slice[0, 0]);
            Assert.Equal(0, slice[0, 1]);
            Assert.Equal(0, slice[1, 0]);
            Assert.Equal(0, slice[1, 1]);
            //First values of original span should be cleared.
            Assert.Equal(0, spanInt[0, 0]);
            Assert.Equal(0, spanInt[0, 1]);
            Assert.Equal(0, spanInt[1, 0]);
            Assert.Equal(0, spanInt[1, 1]);
            //Make sure the rest of the values from the original span didn't get cleared.
            Assert.Equal(3, spanInt[0, 2]);
            Assert.Equal(6, spanInt[1, 2]);
            Assert.Equal(7, spanInt[2, 0]);
            Assert.Equal(8, spanInt[2, 1]);
            Assert.Equal(9, spanInt[2, 2]);


            spanInt.Clear();
            var enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(0, enumerator.Current);
            }

            a = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            spanInt = a.AsTensorSpan([9]);
            slice = spanInt.Slice(0..1);
            slice.Clear();
            Assert.Equal(0, slice[0]);
            //First value of original span should be cleared.
            Assert.Equal(0, spanInt[0]);
            //Make sure the rest of the values from the original span didn't get cleared.
            Assert.Equal(2, spanInt[1]);
            Assert.Equal(3, spanInt[2]);
            Assert.Equal(4, spanInt[3]);
            Assert.Equal(5, spanInt[4]);
            Assert.Equal(6, spanInt[5]);
            Assert.Equal(7, spanInt[6]);
            Assert.Equal(8, spanInt[7]);
            Assert.Equal(9, spanInt[8]);


            spanInt.Clear();
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(0, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 27)];
            spanInt = a.AsTensorSpan([3, 3, 3]);
            spanInt.Clear();
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(0, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 12)];
            spanInt = a.AsTensorSpan([3, 2, 2]);
            spanInt.Clear();
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(0, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 16)];
            spanInt = a.AsTensorSpan([2, 2, 2, 2]);
            spanInt.Clear();
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(0, enumerator.Current);
            }

            a = [.. Enumerable.Range(0, 24)];
            spanInt = a.AsTensorSpan([3, 2, 2, 2]);
            spanInt.Clear();
            enumerator = spanInt.GetEnumerator();
            while (enumerator.MoveNext())
            {
                Assert.Equal(0, enumerator.Current);
            }
        }

        [Fact]
        public static void TensorSpanCopyTest()
        {
            int[] leftData = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            int[] rightData = new int[9];
            TensorSpan<int> leftSpan = leftData.AsTensorSpan([3, 3]);
            TensorSpan<int> rightSpan = rightData.AsTensorSpan([3, 3]);
            leftSpan.CopyTo(rightSpan);
            var leftEnum = leftSpan.GetEnumerator();
            var rightEnum = rightSpan.GetEnumerator();
            while(leftEnum.MoveNext() && rightEnum.MoveNext())
            {
                Assert.Equal(leftEnum.Current, rightEnum.Current);
            }

            //Make sure its a copy
            leftSpan[0, 0] = 100;
            Assert.NotEqual(leftSpan[0, 0], rightSpan[0, 0]);

            // Can't copy if data is not same shape or broadcastable to.
            Assert.Throws<ArgumentException>(() =>
                {
                    leftData = [1, 2, 3, 4, 5, 6, 7, 8, 9];
                    rightData = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
                    TensorSpan<int> leftSpan = leftData.AsTensorSpan([9]);
                    TensorSpan<int> tensor = rightData.AsTensorSpan([rightData.Length]);
                    leftSpan.CopyTo(tensor);
                }
            );

            leftData = [.. Enumerable.Range(0, 27)];
            rightData = [.. Enumerable.Range(0, 27)];
            leftSpan = leftData.AsTensorSpan([3, 3, 3]);
            rightSpan = rightData.AsTensorSpan([3, 3, 3]);
            leftSpan.CopyTo(rightSpan);

            while (leftEnum.MoveNext() && rightEnum.MoveNext())
            {
                Assert.Equal(leftEnum.Current, rightEnum.Current);
            }

            Assert.Throws<ArgumentException>(() =>
            {
                var l = leftData.AsTensorSpan([3, 3, 3]);
                var r = new TensorSpan<int>();
                l.CopyTo(r);
            });
        }

        [Fact]
        public static void TensorSpanTryCopyTest()
        {
            int[] leftData = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            int[] rightData = new int[9];
            TensorSpan<int> leftSpan = leftData.AsTensorSpan([3, 3]);
            TensorSpan<int> rightSpan = rightData.AsTensorSpan([3, 3]);
            var success = leftSpan.TryCopyTo(rightSpan);
            Assert.True(success);
            var leftEnum = leftSpan.GetEnumerator();
            var rightEnum = rightSpan.GetEnumerator();
            while (leftEnum.MoveNext() && rightEnum.MoveNext())
            {
                Assert.Equal(leftEnum.Current, rightEnum.Current);
            }

            //Make sure its a copy
            leftSpan[0, 0] = 100;
            Assert.NotEqual(leftSpan[0, 0], rightSpan[0, 0]);

            leftData = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            rightData = new int[15];
            leftSpan = leftData.AsTensorSpan([9]);
            rightSpan = rightData.AsTensorSpan([15]);
            success = leftSpan.TryCopyTo(rightSpan);

            Assert.False(success);

            leftData = [.. Enumerable.Range(0, 27)];
            rightData = [.. Enumerable.Range(0, 27)];
            leftSpan = leftData.AsTensorSpan([3, 3, 3]);
            rightSpan = rightData.AsTensorSpan([3, 3, 3]);
            success = leftSpan.TryCopyTo(rightSpan);
            Assert.True(success);

            while (leftEnum.MoveNext() && rightEnum.MoveNext())
            {
                Assert.Equal(leftEnum.Current, rightEnum.Current);
            }

            var l = leftData.AsTensorSpan([3, 3, 3]);
            var r = new TensorSpan<int>();
            success = l.TryCopyTo(r);
            Assert.False(success);

            success = new TensorSpan<double>(new double[1]).TryCopyTo(Array.Empty<double>());
            Assert.False(success);
        }

        [Fact]
        public static void TensorSpanSliceTest()
        {
            // Make sure slicing an empty TensorSpan works
            TensorSpan<int> emptyTensorSpan = new TensorSpan<int>(Array.Empty<int>()).Slice(new NRange[] { .. });
            Assert.Equal([0], emptyTensorSpan.Lengths);
            Assert.Equal(1, emptyTensorSpan.Rank);
            Assert.Equal(0, emptyTensorSpan.FlattenedLength);

            // Make sure slicing a multi-dimensional empty TensorSpan works
            int[,] empty2dArray = new int[2, 0];
            emptyTensorSpan = new TensorSpan<int>(empty2dArray);
            TensorSpan<int> slicedEmptyTensorSpan = emptyTensorSpan.Slice(new NRange[] { .. , .. });
            Assert.Equal([2, 0], slicedEmptyTensorSpan.Lengths);
            Assert.Equal(2, slicedEmptyTensorSpan.Rank);
            Assert.Equal(0, slicedEmptyTensorSpan.FlattenedLength);

            slicedEmptyTensorSpan = emptyTensorSpan.Slice(new NRange[] { 0..1, .. });
            Assert.Equal([1, 0], slicedEmptyTensorSpan.Lengths);
            Assert.Equal(2, slicedEmptyTensorSpan.Rank);
            Assert.Equal(0, slicedEmptyTensorSpan.FlattenedLength);

            // Make sure slicing a multi-dimensional empty TensorSpan works
            int[,,,] empty4dArray = new int[2, 5, 1, 0];
            emptyTensorSpan = new TensorSpan<int>(empty4dArray);
            slicedEmptyTensorSpan = emptyTensorSpan.Slice(new NRange[] { .., .., .., .. });
            Assert.Equal([2, 5, 1, 0], slicedEmptyTensorSpan.Lengths);
            Assert.Equal(4, slicedEmptyTensorSpan.Rank);
            Assert.Equal(0, slicedEmptyTensorSpan.FlattenedLength);

            emptyTensorSpan = new TensorSpan<int>(empty4dArray);
            slicedEmptyTensorSpan = emptyTensorSpan.Slice(new NRange[] { 0..1, .., .., .. });
            Assert.Equal([1, 5, 1, 0], slicedEmptyTensorSpan.Lengths);
            Assert.Equal(4, slicedEmptyTensorSpan.Rank);
            Assert.Equal(0, slicedEmptyTensorSpan.FlattenedLength);

            emptyTensorSpan = new TensorSpan<int>(empty4dArray);
            slicedEmptyTensorSpan = emptyTensorSpan.Slice(new NRange[] { 0..1, 2..3, .., .. });
            Assert.Equal([1, 1, 1, 0], slicedEmptyTensorSpan.Lengths);
            Assert.Equal(4, slicedEmptyTensorSpan.Rank);
            Assert.Equal(0, slicedEmptyTensorSpan.FlattenedLength);

            empty4dArray = new int[2, 0, 1, 5];
            emptyTensorSpan = new TensorSpan<int>(empty4dArray);
            slicedEmptyTensorSpan = emptyTensorSpan.Slice(new NRange[] { .., .., .., .. });
            Assert.Equal([2, 0, 1, 5], slicedEmptyTensorSpan.Lengths);
            Assert.Equal(4, slicedEmptyTensorSpan.Rank);
            Assert.Equal(0, slicedEmptyTensorSpan.FlattenedLength);

            int[] a = [1, 2, 3, 4, 5, 6, 7, 8, 9];
            int[] results = new int[9];
            TensorSpan<int> spanInt = a.AsTensorSpan([3, 3]);

            Assert.Throws<ArgumentOutOfRangeException>(() => a.AsTensorSpan([2, 3]).Slice(0..1));
            Assert.Throws<ArgumentOutOfRangeException>(() => a.AsTensorSpan([2, 3]).Slice(1..2));
            Assert.Throws<ArgumentOutOfRangeException>(() => a.AsTensorSpan([2, 3]).Slice(0..1, 5..6));

            var sp = spanInt.Slice(1..3, 1..3);
            Assert.Equal(5, sp[0, 0]);
            Assert.Equal(6, sp[0, 1]);
            Assert.Equal(8, sp[1, 0]);
            Assert.Equal(9, sp[1, 1]);
            int[] slice = [5, 6, 8, 9];
            results = new int[4];
            sp.FlattenTo(results);
            Assert.Equal(slice, results);
            var enumerator = sp.GetEnumerator();
            var index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(slice[index++], enumerator.Current);
            }

            sp = spanInt.Slice(0..3, 0..3);
            Assert.Equal(1, sp[0, 0]);
            Assert.Equal(2, sp[0, 1]);
            Assert.Equal(3, sp[0, 2]);
            Assert.Equal(4, sp[1, 0]);
            Assert.Equal(5, sp[1, 1]);
            Assert.Equal(6, sp[1, 2]);
            Assert.Equal(7, sp[2, 0]);
            Assert.Equal(8, sp[2, 1]);
            Assert.Equal(9, sp[2, 2]);
            results = new int[9];
            sp.FlattenTo(results);
            Assert.Equal(a, results);
            enumerator = sp.GetEnumerator();
            index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(a[index++], enumerator.Current);
            }

            sp = spanInt.Slice(0..1, 0..1);
            Assert.Equal(1, sp[0, 0]);
            Assert.Throws<IndexOutOfRangeException>(() => a.AsTensorSpan([3, 3]).Slice(0..1, 0..1)[0, 1]);
            slice = [1];
            results = new int[1];
            sp.FlattenTo(results);
            Assert.Equal(slice, results);
            enumerator = sp.GetEnumerator();
            index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(slice[index++], enumerator.Current);
            }

            sp = spanInt.Slice(0..2, 0..2);
            Assert.Equal(1, sp[0, 0]);
            Assert.Equal(2, sp[0, 1]);
            Assert.Equal(4, sp[1, 0]);
            Assert.Equal(5, sp[1, 1]);
            slice = [1, 2, 4, 5];
            results = new int[4];
            sp.FlattenTo(results);
            Assert.Equal(slice, results);
            enumerator = sp.GetEnumerator();
            index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(slice[index++], enumerator.Current);
            }

            int[] numbers = [.. Enumerable.Range(0, 27)];
            spanInt = numbers.AsTensorSpan([3, 3, 3]);
            sp = spanInt.Slice(1..2, 1..2, 1..2);
            Assert.Equal(13, sp[0, 0, 0]);
            slice = [13];
            results = new int[1];
            sp.FlattenTo(results);
            Assert.Equal(slice, results);
            enumerator = sp.GetEnumerator();
            index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(slice[index++], enumerator.Current);
            }

            sp = spanInt.Slice(1..3, 1..3, 1..3);
            Assert.Equal(13, sp[0, 0, 0]);
            Assert.Equal(14, sp[0, 0, 1]);
            Assert.Equal(16, sp[0, 1, 0]);
            Assert.Equal(17, sp[0, 1, 1]);
            Assert.Equal(22, sp[1, 0, 0]);
            Assert.Equal(23, sp[1, 0, 1]);
            Assert.Equal(25, sp[1, 1, 0]);
            Assert.Equal(26, sp[1, 1, 1]);
            slice = [13, 14, 16, 17, 22, 23, 25, 26];
            results = new int[8];
            sp.FlattenTo(results);
            Assert.Equal(slice, results);
            enumerator = sp.GetEnumerator();
            index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(slice[index++], enumerator.Current);
            }

            numbers = [.. Enumerable.Range(0, 16)];
            spanInt = numbers.AsTensorSpan([2, 2, 2, 2]);
            sp = spanInt.Slice(1..2, 0..2, 1..2, 0..2);
            Assert.Equal(10, sp[0,0,0,0]);
            Assert.Equal(11, sp[0,0,0,1]);
            Assert.Equal(14, sp[0,1,0,0]);
            Assert.Equal(15, sp[0,1,0,1]);
            slice = [10, 11, 14, 15];
            results = new int[4];
            sp.FlattenTo(results);
            Assert.Equal(slice, results);
            enumerator = sp.GetEnumerator();
            index = 0;
            while (enumerator.MoveNext())
            {
                Assert.Equal(slice[index++], enumerator.Current);
            }
        }

        [Fact]
        public static void LongArrayAsTensorSpan()
        {
            long[] b = { 91, -92, 93, 94, -95 };
            TensorSpan<long> spanLong = b.AsTensorSpan([5]);
            Assert.Equal(91, spanLong[0]);
            Assert.Equal(-92, spanLong[1]);
            Assert.Equal(93, spanLong[2]);
            Assert.Equal(94, spanLong[3]);
            Assert.Equal(-95, spanLong[4]);
        }

        [Fact]
        public static void NullArrayAsTensorSpan()
        {
            int[] a = null;
            TensorSpan<int> span = a.AsTensorSpan();
            Assert.True(span == default);
        }

        [Fact]
        public static void GetSpanTest()
        {
            TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);

            Span<int> span = tensorSpan.GetSpan([0, 0], 16);
            Assert.Equal(16, span.Length);
            Assert.Equal([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15], span);

            span = tensorSpan.GetSpan([1, 1], 3);
            Assert.Equal(3, span.Length);
            Assert.Equal([5, 6, 7], span);

            span = tensorSpan.GetSpan([3, 0], 4);
            Assert.Equal(4, span.Length);
            Assert.Equal([12, 13, 14, 15], span);

            span = tensorSpan.GetSpan([0, 3], 1);
            Assert.Equal(1, span.Length);
            Assert.Equal([3], span);

            span = tensorSpan.GetSpan([3, 3], 1);
            Assert.Equal(1, span.Length);
            Assert.Equal([15], span);
        }

        [Fact]
        public static void GetSpanThrowsForInvalidIndexesTest()
        {
            Assert.Throws<IndexOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([4, 0], 17);
            });

            Assert.Throws<IndexOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([0, 4], 17);
            });

            Assert.Throws<IndexOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([4, 4], 17);
            });
        }

        [Fact]
        public static void GetSpanThrowsForInvalidLengthsTest()
        {
            Assert.Throws<ArgumentOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([0, 0], -1);
            });

            Assert.Throws<ArgumentOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([0, 0], 17);
            });

            Assert.Throws<ArgumentOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([1, 1], 4);
            });

            Assert.Throws<ArgumentOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([3, 0], 5);
            });

            Assert.Throws<ArgumentOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([0, 3], 2);
            });

            Assert.Throws<ArgumentOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.GetSpan([3, 3], 2);
            });
        }

        [Fact]
        public static void TryGetSpanTest()
        {
            TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);

            Assert.True(tensorSpan.TryGetSpan([0, 0], 16, out Span<int> span));
            Assert.Equal(16, span.Length);
            Assert.Equal([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15], span);

            Assert.True(tensorSpan.TryGetSpan([1, 1], 3, out span));
            Assert.Equal(3, span.Length);
            Assert.Equal([5, 6, 7], span);

            Assert.True(tensorSpan.TryGetSpan([3, 0], 4, out span));
            Assert.Equal(4, span.Length);
            Assert.Equal([12, 13, 14, 15], span);

            Assert.True(tensorSpan.TryGetSpan([0, 3], 1, out span));
            Assert.Equal(1, span.Length);
            Assert.Equal([3], span);

            Assert.True(tensorSpan.TryGetSpan([3, 3], 1, out span));
            Assert.Equal(1, span.Length);
            Assert.Equal([15], span);
        }

        [Fact]
        public static void TryGetSpanThrowsForInvalidIndexesTest()
        {
            Assert.Throws<IndexOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.TryGetSpan([4, 0], 17, out Span<int> _);
            });

            Assert.Throws<IndexOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.TryGetSpan([0, 4], 17, out Span<int> _);
            });

            Assert.Throws<IndexOutOfRangeException>(() => {
                TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);
                _ = tensorSpan.TryGetSpan([4, 4], 17, out Span<int> _);
            });
        }

        [Fact]
        public static void TryGetSpanFailsForInvalidLengthsTest()
        {
            TensorSpan<int> tensorSpan = new TensorSpan<int>(Enumerable.Range(0, 16).ToArray(), [4, 4]);

            Assert.False(tensorSpan.TryGetSpan([0, 0], -1, out Span<int> span));
            Assert.Equal(0, span.Length);

            Assert.False(tensorSpan.TryGetSpan([0, 0], 17, out span));
            Assert.Equal(0, span.Length);

            Assert.False(tensorSpan.TryGetSpan([1, 1], 4, out span));
            Assert.Equal(0, span.Length);

            Assert.False(tensorSpan.TryGetSpan([3, 0], 5, out span));
            Assert.Equal(0, span.Length);

            Assert.False(tensorSpan.TryGetSpan([0, 3], 2, out span));
            Assert.Equal(0, span.Length);

            Assert.False(tensorSpan.TryGetSpan([3, 3], 2, out span));
            Assert.Equal(0, span.Length);
        }
    }
}
