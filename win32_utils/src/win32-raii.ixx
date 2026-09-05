module;

#include <Windows.h>

export module win32:raii;

export class VirtualAllocRaii
{
private:
    HANDLE hProcess_;
    LPVOID pMemory_;
    SIZE_T size_;

public:
    VirtualAllocRaii(HANDLE hProcess, SIZE_T size);

    ~VirtualAllocRaii();

    VirtualAllocRaii(const VirtualAllocRaii &) = delete;

    VirtualAllocRaii &operator=(const VirtualAllocRaii &) = delete;

    VirtualAllocRaii(VirtualAllocRaii &&other) noexcept;

    VirtualAllocRaii &operator=(VirtualAllocRaii &&other) noexcept;

    [[nodiscard]] bool IsInvalid();

    [[nodiscard]] LPVOID Get();
};

export class HandleRaii
{
public:
    explicit HandleRaii(HANDLE handle);

    HandleRaii(const HandleRaii &) = delete;

    HandleRaii &operator=(const HandleRaii &) = delete;

    HandleRaii(HandleRaii &&other) noexcept = default;

    HandleRaii &operator=(HandleRaii &&other) noexcept = default;

    ~HandleRaii();

    [[nodiscard]] HANDLE Get() const;

    [[nodiscard]] bool IsInvalid() const;

private:
    HANDLE handle_;
};

VirtualAllocRaii::VirtualAllocRaii(HANDLE hProcess, SIZE_T size) : hProcess_(hProcess), size_(size), pMemory_(nullptr)
{
    pMemory_ = ::VirtualAllocEx(hProcess, nullptr, size, MEM_COMMIT, PAGE_READWRITE);
}

VirtualAllocRaii::~VirtualAllocRaii()
{
    if (pMemory_ != nullptr)
    {
        ::VirtualFreeEx(hProcess_, pMemory_, 0, MEM_RELEASE);
    }
}

VirtualAllocRaii::VirtualAllocRaii(VirtualAllocRaii &&other) noexcept
    : hProcess_(other.hProcess_), pMemory_(other.pMemory_), size_(other.size_)
{
    other.pMemory_ = nullptr;
}

VirtualAllocRaii &VirtualAllocRaii::operator=(VirtualAllocRaii &&other) noexcept
{
    if (this != &other)
    {
        pMemory_ = other.pMemory_;
        hProcess_ = other.hProcess_;
        size_ = other.size_;
        other.pMemory_ = nullptr;
    }
    return *this;
}

bool VirtualAllocRaii::IsInvalid()
{
    return pMemory_ == nullptr;
}

LPVOID VirtualAllocRaii::Get()
{
    return pMemory_;
}

HandleRaii::HandleRaii(HANDLE handle) : handle_(handle)
{
}

HandleRaii::~HandleRaii()
{
    if (!IsInvalid())
    {
        ::CloseHandle(handle_);
    }
}

HANDLE HandleRaii::Get() const
{
    return handle_;
}

bool HandleRaii::IsInvalid() const
{
    return handle_ == INVALID_HANDLE_VALUE || handle_ == nullptr;
}
