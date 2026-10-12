#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <stdexcept>
#include <cstdint>

namespace obsidian {
namespace storage {

enum class StatusCode : uint8_t {
    Ok = 0,
    NotFound = 1,
    AlreadyExists = 2,
    InvalidArgument = 3,
    ConcurrentModification = 4,
    IOError = 5,
    Corruption = 6,
    NotSupported = 7,
    Internal = 8
};

class Status {
public:
    constexpr Status() noexcept : code_(StatusCode::Ok) {}
    
    Status(StatusCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    static Status Ok() noexcept {
        return Status();
    }

    static Status NotFound(std::string message) {
        return Status(StatusCode::NotFound, std::move(message));
    }

    static Status AlreadyExists(std::string message) {
        return Status(StatusCode::AlreadyExists, std::move(message));
    }

    static Status InvalidArgument(std::string message) {
        return Status(StatusCode::InvalidArgument, std::move(message));
    }

    static Status ConcurrentModification(std::string message) {
        return Status(StatusCode::ConcurrentModification, std::move(message));
    }

    static Status IOError(std::string message) {
        return Status(StatusCode::IOError, std::move(message));
    }

    static Status Corruption(std::string message) {
        return Status(StatusCode::Corruption, std::move(message));
    }

    static Status NotSupported(std::string message) {
        return Status(StatusCode::NotSupported, std::move(message));
    }

    static Status Internal(std::string message) {
        return Status(StatusCode::Internal, std::move(message));
    }

    [[nodiscard]] constexpr bool ok() const noexcept {
        return code_ == StatusCode::Ok;
    }

    [[nodiscard]] constexpr StatusCode code() const noexcept {
        return code_;
    }

    [[nodiscard]] const std::string& message() const noexcept {
        return message_;
    }

    [[nodiscard]] std::string ToString() const {
        if (ok()) return "OK";
        std::string s;
        switch (code_) {
            case StatusCode::NotFound: s = "NOT_FOUND: "; break;
            case StatusCode::AlreadyExists: s = "ALREADY_EXISTS: "; break;
            case StatusCode::InvalidArgument: s = "INVALID_ARGUMENT: "; break;
            case StatusCode::ConcurrentModification: s = "CONCURRENT_MODIFICATION: "; break;
            case StatusCode::IOError: s = "IO_ERROR: "; break;
            case StatusCode::Corruption: s = "CORRUPTION: "; break;
            case StatusCode::NotSupported: s = "NOT_SUPPORTED: "; break;
            case StatusCode::Internal: s = "INTERNAL: "; break;
            default: s = "UNKNOWN: "; break;
        }
        s.append(message_);
        return s;
    }

private:
    StatusCode code_;
    std::string message_;
};

template <typename T>
class StatusOr {
public:
    StatusOr(const Status& status) : status_(status) {
        if (status.ok()) {
            throw std::invalid_argument("StatusOr constructed with Status::Ok requires a value");
        }
    }

    StatusOr(Status&& status) : status_(std::move(status)) {
        if (status_.ok()) {
            throw std::invalid_argument("StatusOr constructed with Status::Ok requires a value");
        }
    }

    StatusOr(const T& value) : status_(Status::Ok()), has_value_(true) {
        new (&storage_) T(value);
    }

    StatusOr(T&& value) : status_(Status::Ok()), has_value_(true) {
        new (&storage_) T(std::move(value));
    }

    StatusOr(const StatusOr& other) : status_(other.status_), has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(*other);
        }
    }

    StatusOr(StatusOr&& other) noexcept : status_(std::move(other.status_)), has_value_(other.has_value_) {
        if (has_value_) {
            new (&storage_) T(std::move(*other));
            other.has_value_ = false;
        }
    }

    StatusOr& operator=(const StatusOr& other) {
        if (this != &other) {
            destroy();
            status_ = other.status_;
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_) T(*other);
            }
        }
        return *this;
    }

    StatusOr& operator=(StatusOr&& other) noexcept {
        if (this != &other) {
            destroy();
            status_ = std::move(other.status_);
            has_value_ = other.has_value_;
            if (has_value_) {
                new (&storage_) T(std::move(*other));
                other.has_value_ = false;
            }
        }
        return *this;
    }

    ~StatusOr() {
        destroy();
    }

    [[nodiscard]] bool ok() const noexcept {
        return status_.ok();
    }

    [[nodiscard]] const Status& status() const noexcept {
        return status_;
    }

    [[nodiscard]] const T& value() const & {
        ensure_ok();
        return *reinterpret_cast<const T*>(&storage_);
    }

    [[nodiscard]] T& value() & {
        ensure_ok();
        return *reinterpret_cast<T*>(&storage_);
    }

    [[nodiscard]] T&& value() && {
        ensure_ok();
        return std::move(*reinterpret_cast<T*>(&storage_));
    }

    [[nodiscard]] const T& operator*() const & {
        return value();
    }

    [[nodiscard]] T& operator*() & {
        return value();
    }

    [[nodiscard]] const T* operator->() const {
        return &value();
    }

    [[nodiscard]] T* operator->() {
        return &value();
    }

private:
    void ensure_ok() const {
        if (!ok()) {
            throw std::runtime_error("Attempted to access value of failed StatusOr: " + status_.ToString());
        }
    }

    void destroy() noexcept {
        if (has_value_) {
            reinterpret_cast<T*>(&storage_)->~T();
            has_value_ = false;
        }
    }

    Status status_;
    bool has_value_{false};
    alignas(T) unsigned char storage_[sizeof(T)];
};

} // namespace storage
} // namespace obsidian
