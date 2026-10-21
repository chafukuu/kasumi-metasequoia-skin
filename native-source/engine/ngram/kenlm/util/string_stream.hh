#ifndef UTIL_STRING_STREAM_H
#define UTIL_STRING_STREAM_H

#include "fake_ostream.hh"

#include <cassert>
#include <string>

namespace util {

class StringStream : public FakeOStream<StringStream> {
  public:
    StringStream() {}

    StringStream &flush() { return *this; }

    StringStream &write(const void *data, std::size_t length) {
      out_.append(static_cast<const char*>(data), length);
      return *this;
    }

    const std::string &str() const { return out_; }

    void str(const std::string &val) { out_ = val; }

    void swap(std::string &str) { std::swap(out_, str); }

  protected:
    friend class FakeOStream<StringStream>;
    char *Ensure(std::size_t amount) {
      std::size_t current = out_.size();
      out_.resize(out_.size() + amount);
      return &out_[current];
    }

    void AdvanceTo(char *to) {
      // Local patch (MSVC compatibility): &*end() dereferences the end
      // iterator, which the MSVC checked iterators reject even in asserts;
      // data() + size() is the same past-the-end pointer without UB.
      assert(to <= out_.data() + out_.size());
      assert(to >= out_.data());
      out_.resize(to - out_.data());
    }

  private:
    std::string out_;
};

} // namespace

#endif // UTIL_STRING_STREAM_H
