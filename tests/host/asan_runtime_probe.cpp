#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <memory>

int main(int argc, char* argv[])
{
    if(argc != 2)
    {
        return 2;
    }

    char* end = nullptr;
    errno = 0;
    const auto index = std::strtoul(argv[1], &end, 10);
    if(errno != 0 || end == argv[1] || *end != '\0')
    {
        return 2;
    }

    constexpr std::size_t bufferSize = 8;
    auto buffer = std::make_unique<unsigned char[]>(bufferSize);
    // Deliberately cross the allocation boundary; the driver requires ASan's
    // heap-buffer-overflow diagnostic before it accepts this probe.
    buffer[index] = 0x2a;
    return buffer[0];
}
