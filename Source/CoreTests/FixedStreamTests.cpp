#include <CppUnitTest.h>
#include <tge/util/FixedStream.h>
#include <string>
#include <exception>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Tga;

namespace FixedStreamTests
{
    TEST_CLASS(FixedStreamBasicTests)
    {
    public:

        TEST_METHOD(DefaultConstruction)
        {
            FixedStream<16> stream;
            Assert::AreEqual("", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(0), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(16), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(AppendSmallStringFixed)
        {
            FixedStream<16> stream;
            stream << "Hello";
            Assert::AreEqual("Hello", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(5), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(16), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(AppendLargeStringDynamic)
        {
            FixedStream<16> stream;
            stream << "This is a very long string that should exceed fixed buffer.";
            Assert::AreEqual("This is a very long string that should exceed fixed buffer.", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(59), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsTrue(stream.IsUsingDynamicStorage());
            Assert::IsTrue(stream.GetCapacity() > 16u);
        }

        TEST_METHOD(WriteFormatted)
        {
            FixedStream<32> stream;
            stream.WriteFormatted("Value: %d, Text: %s", 123, "test");
            Assert::AreEqual("Value: 123, Text: test", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(22), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
        }

        TEST_METHOD(CopyConstructorFixedToFixed)
        {
            FixedStream<16> original;
            original << "Source";
            FixedStream<16> copy = original;
            Assert::AreEqual("Source", copy.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(6), static_cast<unsigned long long>(copy.GetSize()));
            Assert::IsFalse(copy.IsUsingDynamicStorage());
            Assert::AreEqual("Source", original.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(6), static_cast<unsigned long long>(original.GetSize()));
            Assert::IsFalse(original.IsUsingDynamicStorage());
        }

        TEST_METHOD(CopyConstructorDynamicToDynamic)
        {
            FixedStream<16> original;
            original << "This is a long string to force dynamic allocation.";
            FixedStream<16> copy = original;
            Assert::AreEqual("This is a long string to force dynamic allocation.", copy.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(50), static_cast<unsigned long long>(copy.GetSize()));
            Assert::IsTrue(copy.IsUsingDynamicStorage());
            Assert::AreEqual("This is a long string to force dynamic allocation.", original.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(50), static_cast<unsigned long long>(original.GetSize()));
            Assert::IsTrue(original.IsUsingDynamicStorage());
        }

        TEST_METHOD(MoveConstructor)
        {
            FixedStream<16> original;
            original << "Move me!";
            FixedStream<16> moved = std::move(original);
            Assert::AreEqual("Move me!", moved.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(8), static_cast<unsigned long long>(moved.GetSize()));
            Assert::AreEqual("", original.GetData()); // Line 87
            Assert::AreEqual(static_cast<unsigned long long>(0), static_cast<unsigned long long>(original.GetSize()));
        }

        TEST_METHOD(CopyAssignment)
        {
            FixedStream<16> stream1, stream2;
            stream1 << "Hello World!";
            stream2 = stream1;
            Assert::AreEqual("Hello World!", stream2.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(12), static_cast<unsigned long long>(stream2.GetSize()));
            Assert::IsFalse(stream2.IsUsingDynamicStorage());
        }

        TEST_METHOD(MoveAssignment)
        {
            FixedStream<16> stream1, stream2;
            stream1 << "Hello Dynamic World! This is a very very long string."; // 53 characters
            stream2 = std::move(stream1);
            Assert::AreEqual("Hello Dynamic World! This is a very very long string.", stream2.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(53), static_cast<unsigned long long>(stream2.GetSize()));
            Assert::AreEqual("", stream1.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(0), static_cast<unsigned long long>(stream1.GetSize()));
        }

        TEST_METHOD(ResizeGrowFixedWithZeros)
        {
            FixedStream<20> stream;
            stream << "Test";
            stream.Resize(10);
            Assert::AreEqual("Test\0\0\0\0\0\0", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(10), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(20), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(ResizeShrinkFixed)
        {
            FixedStream<20> stream;
            stream << "LongString";
            stream.Resize(5);
            Assert::AreEqual("LongS", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(5), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(20), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(ResizeGrowToDynamic)
        {
            FixedStream<10> stream;
            stream << "Short";
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            stream.Resize(15);
            Assert::AreEqual(static_cast<unsigned long long>(15), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsTrue(stream.IsUsingDynamicStorage());
            Assert::IsTrue(stream.GetCapacity() >= 16u);
            Assert::AreEqual('S', stream.GetData()[0]);
            Assert::AreEqual('h', stream.GetData()[1]);
            Assert::AreEqual('o', stream.GetData()[2]);
            Assert::AreEqual('r', stream.GetData()[3]);
            Assert::AreEqual('t', stream.GetData()[4]);
            for (uint32_t i = 5; i < 15; ++i) {
                Assert::AreEqual('\0', stream.GetData()[i]);
            }
            Assert::AreEqual('\0', stream.GetData()[15]);
        }

        TEST_METHOD(ResizeShrinkDynamicKeepsCapacity)
        {
            FixedStream<10> stream;
            stream << "This string is long enough to go dynamic.";
            std::size_t initialDynamicCapacity = stream.GetCapacity();
            Assert::IsTrue(stream.IsUsingDynamicStorage());

            stream.Resize(5);
            Assert::AreEqual("This ", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(5), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsTrue(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(initialDynamicCapacity), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(StringViewAppendNormal)
        {
            FixedStream<32> stream;
            std::string_view sv = "Hello, StringView!";
            stream << sv;
            Assert::AreEqual("Hello, StringView!", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(18), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            Assert::AreEqual(std::string(sv.data(), sv.size()), std::string(stream.GetStringView().data(), stream.GetStringView().size()));
        }

        TEST_METHOD(StringViewAppendWithEmbeddedNull)
        {
            FixedStream<32> stream;
            const char* data_with_null = "Part1\0Part2";
            std::string_view sv(data_with_null, 11);
            stream << sv;

            Assert::AreEqual(static_cast<unsigned long long>(11), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());

            Assert::AreEqual('P', stream.GetData()[0]);
            Assert::AreEqual('a', stream.GetData()[1]);
            Assert::AreEqual('r', stream.GetData()[2]);
            Assert::AreEqual('t', stream.GetData()[3]);
            Assert::AreEqual('1', stream.GetData()[4]);
            Assert::AreEqual('\0', stream.GetData()[5]);
            Assert::AreEqual('P', stream.GetData()[6]);
            Assert::AreEqual('a', stream.GetData()[7]);
            Assert::AreEqual('r', stream.GetData()[8]);
            Assert::AreEqual('t', stream.GetData()[9]);
            Assert::AreEqual('2', stream.GetData()[10]);
            Assert::AreEqual('\0', stream.GetData()[11]);

            Assert::AreEqual(std::string(sv.data(), sv.size()), std::string(stream.GetStringView().data(), stream.GetStringView().size()));
        }

        TEST_METHOD(StringViewAppendChain)
        {
            FixedStream<32> stream;
            stream << "One" << std::string_view("Two", 3) << "Three" << std::string_view("Four", 4);
            Assert::AreEqual("OneTwoThreeFour", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(15), static_cast<unsigned long long>(stream.GetSize()));
        }

        TEST_METHOD(AppendEmptyStringView)
        {
            FixedStream<16> stream;
            stream << "Prefix";
            stream << std::string_view("", 0);
            Assert::AreEqual("Prefix", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(6), static_cast<unsigned long long>(stream.GetSize()));
        }

        TEST_METHOD(AppendNullptrConstChar)
        {
            FixedStream<16> stream;
            stream << "Test";
            try
            {
                stream << (const char*)nullptr;
            }
            catch (const std::exception&)
            {
                Assert::Fail(L"An unexpected standard exception was thrown during nullptr append.");
            }
            catch (...)
            {
                Assert::Fail(L"An unexpected unknown exception was thrown during nullptr append.");
            }
            Assert::AreEqual("Test", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(4), static_cast<unsigned long long>(stream.GetSize()));
        }

        TEST_METHOD(ClearWithResize)
        {
            FixedStream<16> stream;
            stream << "Some content";
            stream.Resize(0);
            Assert::AreEqual("", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(0), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsFalse(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(16), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(ClearDynamicWithResize)
        {
            FixedStream<16> stream;
            stream << "This is definitely going to exceed the fixed buffer capacity and use dynamic storage.";
            Assert::IsTrue(stream.IsUsingDynamicStorage());
            std::size_t initialCapacity = stream.GetCapacity();

            stream.Resize(0);
            Assert::AreEqual("", stream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(0), static_cast<unsigned long long>(stream.GetSize()));
            Assert::IsTrue(stream.IsUsingDynamicStorage());
            Assert::AreEqual(static_cast<unsigned long long>(initialCapacity), static_cast<unsigned long long>(stream.GetCapacity()));
        }

        TEST_METHOD(BracketOperatorAccess)
        {
            // Test in fixed storage mode
            FixedStream<16> fixedStream;
            fixedStream << "HelloWorld"; // Size 10
            Assert::AreEqual('H', fixedStream[0]);
            Assert::AreEqual('o', fixedStream[4]);
            Assert::AreEqual('d', fixedStream[9]);

            fixedStream[0] = 'h';
            fixedStream[5] = '-';
            Assert::AreEqual("hello-orld", fixedStream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(10), static_cast<unsigned long long>(fixedStream.GetSize()));

            // Test const access
            const FixedStream<16>& constFixedStream = fixedStream;
            Assert::AreEqual('h', constFixedStream[0]);
            Assert::AreEqual('-', constFixedStream[5]);

            // Test in dynamic storage mode
            FixedStream<4> dynamicStream; // Small buffer to force dynamic
            dynamicStream << "LongDynamicString"; // Size 17
            Assert::IsTrue(dynamicStream.IsUsingDynamicStorage());
            Assert::AreEqual('L', dynamicStream[0]);
            Assert::AreEqual('g', dynamicStream[3]);
            Assert::AreEqual('m', dynamicStream[8]);
            Assert::AreEqual('g', dynamicStream[16]);

            dynamicStream[0] = 'l';
            dynamicStream[10] = 'X'; // 'c' at index 10 changes to 'X'
            Assert::AreEqual("longDynamiXString", dynamicStream.GetData());
            Assert::AreEqual(static_cast<unsigned long long>(17), static_cast<unsigned long long>(dynamicStream.GetSize()));

            // Test const access for dynamic
            const FixedStream<4>& constDynamicStream = dynamicStream;
            Assert::AreEqual('l', constDynamicStream[0]);
            Assert::AreEqual('X', constDynamicStream[10]);
        }
    };
}