#pragma once
#include <cstring>
#include <memory>
#include <utils/file_ptr.hpp>
class ConstArray {
public:
  virtual char *ptr() { return nullptr; }
  virtual ~ConstArray() {}
};

class ConstBufferIO {
public:
  virtual void update_offset(size_t offset) = 0;
  virtual void write(size_t offset, void *src, size_t size) = 0;
  virtual std::unique_ptr<ConstArray> get_buffer(size_t offset,
                                                 size_t size) = 0;
  virtual std::vector<char> read(size_t offset, size_t size) = 0;
};

static void write_to_file(FILE *file, size_t offset, void *src, size_t size) {
  auto old_offset = ftell64(file);
  fseek64(file, offset, SEEK_CUR);
  auto written = fwrite(src, 1, size, file);
  // rewind
  fseek64(file, old_offset, SEEK_SET);
}

class TmpFileBuffer : public ConstArray {
public:
  TmpFileBuffer(FILE *file, size_t offset, size_t size) {
    file_ = file;
    offset_ = offset;
    size_ = size;
    data_ = (char *)malloc(size);
    memset(data_, 0, size);
  }
  char *ptr() override final { return data_; }
  virtual ~TmpFileBuffer() override final {
    write_to_file(file_, offset_, data_, size_);
    free(data_);
  }

private:
  char *data_;
  FILE *file_;
  size_t offset_;
  size_t size_;
};

class TmpFileConst : public ConstBufferIO {
public:
  TmpFileConst(FILE *file_ptr) { this->file_ = file_ptr; }
  void update_offset(size_t offset) override final {
    fseek64(this->file_, offset, SEEK_CUR);
  }

  std::unique_ptr<ConstArray> get_buffer(size_t offset,
                                         size_t size) override final {
    return std::make_unique<TmpFileBuffer>(file_, offset, size);
  }
  void write(size_t offset, void *src, size_t size) override final {
    write_to_file(file_, offset, src, size);
  }

  std::vector<char> read(size_t offset, size_t size) override final {
    auto old_offset = ftell64(file_);
    fseek64(file_, offset, SEEK_CUR);
    std::vector<char> ret(size);
    fread(ret.data(), size, 1, file_);
    fseek64(file_, old_offset, SEEK_SET);
    return ret;
  }

private:
  FILE *file_;
};

class BoBuffer : public ConstArray {
public:
  BoBuffer(char *ptr) { ptr_ = ptr; }
  char *ptr() override final { return ptr_; }

private:
  char *ptr_;
};

class BoConst : public ConstBufferIO {
public:
  BoConst(void *buffer) { this->buffer_ = (char *)buffer; }
  void update_offset(size_t offset) override final { this->buffer_ += offset; }

  std::unique_ptr<ConstArray> get_buffer(size_t offset,
                                         size_t size) override final {
    return std::make_unique<BoBuffer>(buffer_ + offset);
  }

  void write(size_t offset, void *src, size_t size) override final {
    memcpy(buffer_ + offset, src, size);
  }
  std::vector<char> read(size_t offset, size_t size) override final {
    std::vector<char> ret(size);
    memcpy(ret.data(), buffer_ + offset, size);
    return ret;
  }

private:
  char *buffer_;
};