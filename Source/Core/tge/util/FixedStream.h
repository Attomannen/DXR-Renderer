#pragma once

#include <array>
#include <vector>
#include <cstdio>
#include <string_view>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <utility>
#include <stdexcept>
#include <cstdarg>

namespace Tga
{

    /// FixedStream is a helper for creating strings without allocations.
    /// It uses an inline buffer (std::array) when possible and switches to a dynamic
    /// buffer (std::vector) if the base capacity is exceeded.
    template <std::size_t N = 128>
    class FixedStream
    {
    private:
        union Buffer
        {
            std::array<char, N> myFixed;
            std::vector<char> myDynamic;

            Buffer() { }
            ~Buffer() {}
        } myBuffer;

        uint32_t myUsed = 0;
        bool myUsingDynamicStorage = false;

        void DestroyStorage()
        {
            if (myUsingDynamicStorage)
            {
                myBuffer.myDynamic.~vector<char>();
            }
        }

        void CopyFrom(const FixedStream& other)
        {
            DestroyStorage();

            myUsed = other.myUsed;
            myUsingDynamicStorage = other.myUsingDynamicStorage;

            if (!myUsingDynamicStorage)
            {
                new (&myBuffer.myFixed) std::array<char, N>(other.myBuffer.myFixed);
            }
            else
            {
                new (&myBuffer.myDynamic) std::vector<char>(other.myBuffer.myDynamic);
            }
        }

        /// Resets the moved-from object to a valid, empty state using fixed storage.
        void MoveFrom(FixedStream&& other) noexcept
        {
            DestroyStorage();

            myUsed = other.myUsed;
            myUsingDynamicStorage = other.myUsingDynamicStorage;

            if (other.myUsingDynamicStorage)
            {
                new (&myBuffer.myDynamic) std::vector<char>(std::move(other.myBuffer.myDynamic));
            }
            else
            {
                new (&myBuffer.myFixed) std::array<char, N>(other.myBuffer.myFixed);
            }

            if (other.myUsingDynamicStorage) {
                other.myBuffer.myDynamic.~vector<char>();
            }
            new (&other.myBuffer.myFixed) std::array<char, N>();
            other.myBuffer.myFixed[0] = '\0';
            other.myUsed = 0;
            other.myUsingDynamicStorage = false;
        }

        /// Prepares the internal buffer for writing `needed` characters, switching to dynamic storage if necessary.
        char* PrepareForWrite(std::size_t needed)
        {
            if (!myUsingDynamicStorage)
            {
                if (static_cast<std::size_t>(myUsed) + needed + 1 <= N)
                {
                    return myBuffer.myFixed.data() + myUsed;
                }
                else
                {
                    std::vector<char> dyn;
                    dyn.reserve(static_cast<std::size_t>(myUsed) + needed + 1);
                    dyn.assign(myBuffer.myFixed.data(), myBuffer.myFixed.data() + myUsed);

                    new (&myBuffer.myDynamic) std::vector<char>(std::move(dyn));
                    myUsingDynamicStorage = true;
                    myBuffer.myDynamic.resize(static_cast<std::size_t>(myUsed) + needed + 1);
                    return myBuffer.myDynamic.data() + myUsed;
                }
            }
            else
            {
                myBuffer.myDynamic.resize(static_cast<std::size_t>(myUsed) + needed + 1);
                return myBuffer.myDynamic.data() + myUsed;
            }
        }

        void Append(const char* s_data, std::size_t s_size)
        {
            if (s_size == 0 || s_data == nullptr)
                return;

            char* dest = PrepareForWrite(s_size);
            std::memcpy(dest, s_data, s_size);
            myUsed += static_cast<uint32_t>(s_size);

            if (!myUsingDynamicStorage) {
                myBuffer.myFixed[myUsed] = '\0';
            }
            else {
                myBuffer.myDynamic[myUsed] = '\0';
            }
        }

    public:
        FixedStream() : myUsed(0), myUsingDynamicStorage(false)
        {
            myBuffer.myFixed[0] = '\0';
        }

        FixedStream(const FixedStream& other) { CopyFrom(other); }
        FixedStream(FixedStream&& other) noexcept { MoveFrom(std::move(other)); }

        FixedStream& operator=(const FixedStream& other)
        {
            if (this != &other)
            {
                DestroyStorage();
                CopyFrom(other);
            }
            return *this;
        }

        FixedStream& operator=(FixedStream&& other) noexcept
        {
            if (this != &other)
            {
                DestroyStorage();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        ~FixedStream()
        {
            DestroyStorage();
        }

        /// Formats and appends a string using printf-style formatting.
        template <typename... Args>
        FixedStream& WriteFormatted(const char* fmt, Args... args)
        {
            int needed = std::snprintf(nullptr, 0, fmt, args...);
            if (needed < 0)
                return *this;

            char* dest = PrepareForWrite(static_cast<std::size_t>(needed));
            std::snprintf(dest, needed + 1, fmt, args...);

            myUsed += static_cast<uint32_t>(needed);
            return *this;
        }

        FixedStream& WriteFormatted(const char* fmt, std::va_list args)
        {
            std::va_list argsCopy;
            va_copy(argsCopy, args);
            int needed = std::vsnprintf(nullptr, 0, fmt, argsCopy);
            va_end(argsCopy);

            if (needed <= 0)
                return *this;

            char* dest = PrepareForWrite(static_cast<std::size_t>(needed));
            std::vsnprintf(dest, needed + 1, fmt, args);

            myUsed += static_cast<uint32_t>(needed);
            return *this;
        }

        FixedStream& operator<<(const char* s) {
            if (s == nullptr) {
                return *this;
            }
            return WriteFormatted("%s", s);
        }
        FixedStream& operator<<(std::string_view s) { Append(s.data(), s.size()); return *this; }
        FixedStream& operator<<(char c) { return WriteFormatted("%c", c); }
        FixedStream& operator<<(int i) { return WriteFormatted("%d", i); }
        FixedStream& operator<<(unsigned int i) { return WriteFormatted("%u", i); }
        FixedStream& operator<<(long l) { return WriteFormatted("%ld", l); }
        FixedStream& operator<<(unsigned long l) { return WriteFormatted("%lu", l); }
        FixedStream& operator<<(long long ll) { return WriteFormatted("%lld", ll); }
        FixedStream& operator<<(unsigned long long ull) { return WriteFormatted("%llu", ull); }
        FixedStream& operator<<(double d) { return WriteFormatted("%f", d); }
        FixedStream& operator<<(float f) { return WriteFormatted("%f", f); }

        /// Resizes the stream to the specified count.
        /// If `count` is less than the current size, the string is truncated.
        /// If `count` is greater, the string is extended with null characters.
        void Resize(uint32_t count)
        {
            if (count == myUsed)
            {
                return;
            }

            if (count < myUsed)
            {
                myUsed = count;
                if (!myUsingDynamicStorage)
                {
                    myBuffer.myFixed[myUsed] = '\0';
                }
                else
                {
                    myBuffer.myDynamic[myUsed] = '\0';
                }
            }
            else // Grow
            {
                uint32_t oldUsed = myUsed;
                myUsed = count;

                if (!myUsingDynamicStorage)
                {
                    if (static_cast<std::size_t>(count) < N)
                    {
                        std::memset(myBuffer.myFixed.data() + oldUsed, '\0', static_cast<std::size_t>(count - oldUsed));
                        myBuffer.myFixed[myUsed] = '\0';
                    }
                    else
                    {
                        std::vector<char> dyn;
                        dyn.reserve(static_cast<std::size_t>(count) + 1);
                        dyn.assign(myBuffer.myFixed.data(), myBuffer.myFixed.data() + oldUsed);

                        new (&myBuffer.myDynamic) std::vector<char>(std::move(dyn));
                        myUsingDynamicStorage = true;
                        myBuffer.myDynamic.resize(static_cast<std::size_t>(count) + 1);
                        std::memset(myBuffer.myDynamic.data() + oldUsed, '\0', static_cast<std::size_t>(count - oldUsed));
                        myBuffer.myDynamic[myUsed] = '\0';
                    }
                }
                else
                {
                    myBuffer.myDynamic.resize(static_cast<std::size_t>(count) + 1);
                    std::memset(myBuffer.myDynamic.data() + oldUsed, '\0', static_cast<std::size_t>(count - oldUsed));
                    myBuffer.myDynamic[myUsed] = '\0';
                }
            }
        }

        void Clear()
        {
            Resize(0);
        }

        [[nodiscard]] char* GetData()
        {
            return myUsingDynamicStorage ? myBuffer.myDynamic.data() : myBuffer.myFixed.data();
        }

        [[nodiscard]] const char* GetData() const
        {
            return myUsingDynamicStorage ? myBuffer.myDynamic.data() : myBuffer.myFixed.data();
        }

        [[nodiscard]] uint32_t GetSize() const
        {
            return myUsed;
        }

        [[nodiscard]] std::string_view GetStringView() const
        {
            return std::string_view(GetData(), myUsed);
        }

        [[nodiscard]] bool IsUsingDynamicStorage() const
        {
            return myUsingDynamicStorage;
        }

        [[nodiscard]] std::size_t GetCapacity() const
        {
            return myUsingDynamicStorage ? myBuffer.myDynamic.capacity() : N;
        }

        char& operator[](uint32_t index)
        {
            if (index >= myUsed)
            {
                throw std::out_of_range("FixedStream: index out of bounds");
            }
            return myUsingDynamicStorage ? myBuffer.myDynamic[index] : myBuffer.myFixed[index];
        }

        /// Standardizes slashes ('\' -> '/') and collapses consecutive slashes ('//' -> '/'), preserving character casing.
        FixedStream& NormalizePath()
        {
            if (myUsed == 0)
                return *this;

            char* data = GetData();
            std::size_t writeIdx = 0;
            bool prevWasSlash = false;

            for (std::size_t i = 0; i < myUsed; ++i)
            {
                char c = data[i];
                if (c == '\\')
                    c = '/';

                if (c == '/')
                {
                    if (prevWasSlash && writeIdx > 0)
                        continue;
                    prevWasSlash = true;
                }
                else
                {
                    prevWasSlash = false;
                }

                data[writeIdx++] = c;
            }

            myUsed = static_cast<uint32_t>(writeIdx);
            data[myUsed] = '\0';
            return *this;
        }

        const char& operator[](uint32_t index) const
        {
            if (index >= myUsed)
            {
                throw std::out_of_range("FixedStream: index out of bounds");
            }
            return myUsingDynamicStorage ? myBuffer.myDynamic[index] : myBuffer.myFixed[index];
        }
    };

    constexpr std::size_t FILE_PATH_BUFFER_SIZE = 512;
    using FilePathStream = FixedStream<FILE_PATH_BUFFER_SIZE>;
} // namespace Tga