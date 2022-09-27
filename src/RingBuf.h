// Copyright (c) 2021 miq1 @ gmx . de

#ifndef _RINGBUF_H
#define _RINGBUF_H

#if defined(ESP8266)
#define USE_MUTEX 0
#elif defined(ESP32)
#define USE_MUTEX 1
#endif

#if USE_MUTEX
#include <mutex>   // NOLINT
using std::mutex;
using std::lock_guard;
#define LOCK_GUARD(x,y) std::lock_guard<std::mutex> x(y);
#else
#define LOCK_GUARD(x, y)
#endif

#include <Arduino.h>
#include <iterator>

// RingBuf implements a circular buffer of chosen size.
// No exceptions thrown at all. Memory allocation failures will result in a const
// buffer pointing to the static "nilBuf"!
// template <typename T>
template <typename T>
class RingBuf {
public:
// Fallback static minimal buffer if memory allocation failed etc.
  static const T nilBuf[2];

// Constructor
// size: required size in T elements
// preserve: if true, no more elements will be added to a full buffer unless older elements are consumed
//           if false, buffer will be rotated until the newest element is added
  explicit RingBuf(size_t size = 256, bool preserve = false) noexcept;

  // Destructor: takes care of cleaning up the buffer
  ~RingBuf();

  // Copy constructor
  RingBuf(const RingBuf &r) noexcept;

  // Move constructor
  RingBuf(RingBuf &&r);

  // Copy assignment
  RingBuf& operator=(const RingBuf &r);

  // Move assignment
  RingBuf& operator=(RingBuf &&r);

  // size: get number of elements currently in buffer
  // WARNING! due to the nature of the rolling buffer, this size is VOLATILE and needs to be 
  //          read again every time the buffer is used! Else data may be missed.
  size_t size();

  // data: get start address of the elements in buffer
  const T *data();

  // empty: returns true if no elements are in the buffer
  bool empty();

  // valid: returns true if a buffer was allocated and is usable
  // Using the object in bool context returns the same information
  bool valid();
  operator bool();

  // capacity: return number of unused elements in buffer
  // WARNING! due to the nature of the rolling buffer, this size is VOLATILE and needs to be 
  //          read again every time the buffer is used! Else data may be missed.
  size_t capacity();

  // clear: empty the buffer
  bool clear();

  // pop: remove the leading numElements elements from the buffer
  size_t pop(size_t numElements);

  // operator[]: return the element the index is pointing to. If index is
  // outside the currently used area, return 0
  const T operator[](size_t index);

  // safeCopy: get a stable data copy from currently used buffer
  // target: buffer to copy data into
  // len: number of elements requested
  // move: if true, copied elements will be pop()-ped
  // returns number of elements actually transferred
  size_t safeCopy(T *target, size_t tLen, bool move = false);

  // push_back: add a single element or a buffer of elements to the end of the buffer. 
  // If there is not enough room, the buffer will be rolled until the added elements will fit.
  bool push_back(const T c);
  bool push_back(const T *data, size_t size);

  // Equality comparison: are sizes and contents of two buffers identical?
  bool operator==(RingBuf &r);

  // Iterator: simple forward-only iterator over the visible elements of the buffer
  struct Iterator 
  {
    using iterator_category = std::forward_iterator_tag;
    using difference_type   = std::ptrdiff_t;
    using value_type        = T;
    using pointer           = T*;
    using reference         = T&;

    explicit Iterator(pointer ptr) : m_ptr(ptr) {}

    reference operator*() const { return *m_ptr; }
    pointer operator->() { return m_ptr; }
    Iterator& operator++() { m_ptr++; return *this; }  
    Iterator operator++(int) { Iterator tmp = *this; ++(*this); return tmp; }
    friend bool operator== (const Iterator& a, const Iterator& b) { return a.m_ptr == b.m_ptr; };
    friend bool operator!= (const Iterator& a, const Iterator& b) { return a.m_ptr != b.m_ptr; };  

    private:
      pointer m_ptr;
  };

  // Provide begin() and end()
  Iterator begin() { return Iterator(RB_begin); }
  Iterator end()   { return Iterator(RB_end); }

  // bufferAdr: return the start address of the underlying, double-sized data buffer.
  // bufferSize: return the real length of the underlying data buffer.
  // Note that this is only sensible in a debug context!
  inline const uint8_t *bufferAdr() { return (uint8_t *)RB_buffer; }
  inline const size_t bufferSize() { return RB_len * RB_elementSize; }

protected:
  T *RB_buffer;           // The data buffer proper (twice the requested size)
  T *RB_begin;            // Pointer to the first element currently used
  T *RB_end;              // Pointer behind the last element currently used
  size_t RB_len;                // Real length of buffer (twice the requested size)
  size_t RB_usable;             // Requested length of the buffer
  bool RB_preserve;             // Flag to hold or discard the oldest elements if elements are added
  size_t RB_elementSize;        // Size of a single buffer element
#if USE_MUTEX
  std::mutex m;              // Mutex to protect pop, clear and push_back operations
#endif
  void setFail();            // Internal function to set the object to nilBuf
  void moveFront(size_t numElements); // Move buffer far left internally
};

template <typename T>
const T  RingBuf<T>::nilBuf[2] = { 0, 0 };

// setFail: in case of memory allocation problems, use static nilBuf 
template <typename T>
void RingBuf<T>::setFail() {
  RB_buffer = (T *)RingBuf<T>::nilBuf;
  RB_len = 2 * RB_elementSize;
  RB_usable = 0;
  RB_begin = RB_end = RB_buffer;
}

// valid: return if buffer is a real one
template <typename T>
bool RingBuf<T>::valid() {
  return (RB_buffer && (RB_buffer != RingBuf<T>::nilBuf));
}

// operator bool: same as valid()
template <typename T>
RingBuf<T>::operator bool() {
  return valid();
}

// Constructor: allocate a buffer twice the requested size
template <typename T>
RingBuf<T>::RingBuf(size_t size, bool p) noexcept :
  RB_len(size),
  RB_usable(size),
  RB_preserve(p),
  RB_elementSize(sizeof(T)) {
  // Allocate memory
  RB_buffer = new T[RB_len];
  // Failed?
  if (!RB_buffer) setFail();
  else clear();
}

// Destructor: free allocated memory, if any
template <typename T>
RingBuf<T>::~RingBuf() {
  // Do we have a valid buffer?
  if (valid()) {
    // Yes, free it
    delete RB_buffer;
  }
}

// Copy constructor: take over everything
template <typename T>
RingBuf<T>::RingBuf(const RingBuf &r) noexcept {
  // Is the assigned RingBuf valid?
  if (r.RB_buffer && (r.RB_buffer != RingBuf<T>::nilBuf)) {
    // Yes. Try to allocate a copy
    RB_buffer = new T[r.RB_len];
    // Succeeded?
    if (RB_buffer) {
      // Yes. copy over data
      RB_len = r.RB_len;
      memcpy(RB_buffer, r.RB_buffer, RB_len * r.RB_elementSize);
      RB_begin = RB_buffer + (r.RB_begin - r.RB_buffer);
      RB_end = RB_buffer + (r.RB_end - r.RB_buffer);
      RB_preserve = r.RB_preserve;
      RB_usable = r.RB_usable;
      RB_elementSize = r.RB_elementSize;
    } else {
      setFail();
    }
  } else {
    setFail();
  }
}

// Move constructor
template <typename T>
RingBuf<T>::RingBuf(RingBuf &&r) {
  // Is the assigned RingBuf valid?
  if (r.RB_buffer && (r.RB_buffer != RingBuf<T>::nilBuf)) {
    // Yes. Take over the data
    RB_buffer = r.RB_buffer;
    RB_len = r.RB_len;
    RB_begin = RB_buffer + (r.RB_begin - r.RB_buffer);
    RB_end = RB_buffer + (r.RB_end - r.RB_buffer);
    RB_preserve = r.RB_preserve;
    RB_usable = r.RB_usable;
    RB_elementSize = r.RB_elementSize;
    r.RB_buffer = nullptr;
  } else {
    setFail();
  }
}

// Assignment
template <typename T>
RingBuf<T>& RingBuf<T>::operator=(const RingBuf<T> &r) {
  if (valid()) {
    // Is the source a real RingBuf?
    if (r.RB_buffer && (r.RB_buffer != RingBuf<T>::nilBuf)) {
      // Yes. Copy over the data
      clear();
      push_back(r.RB_begin, r.RB_end - r.RB_begin);
    }
  }
  return *this;
}

// Move assignment
template <typename T>
RingBuf<T>& RingBuf<T>::operator=(RingBuf<T> &&r) {
  if (valid()) {
    // Is the source a real RingBuf?
    if (r.RB_buffer && (r.RB_buffer != RingBuf<T>::nilBuf)) {
      // Yes. Copy over the data
      clear();
      push_back(r.RB_begin, r.RB_end - r.RB_begin);
      r.RB_buffer = nullptr;
    }
  }
  return *this;
}

// size: number of elements used in the buffer
template <typename T>
size_t RingBuf<T>::size() {
  return RB_end - RB_begin;
}

// data: get start of used data area
template <typename T>
const T *RingBuf<T>::data() {
  return RB_begin;
}

// empty: is any data in buffer?
template <typename T>
bool RingBuf<T>::empty() {
  return ((size() == 0) || !valid());
}

// capacity: return remaining usable size
template <typename T>
size_t RingBuf<T>::capacity() {
  if (!valid()) return 0;
  return RB_usable - size();
}

// clear: forget about contents
template <typename T>
bool RingBuf<T>::clear() {
  if (!valid()) return false;
  LOCK_GUARD(cLock, m);
  RB_begin = RB_end = RB_buffer;
  return true;
}

// pop: remove elements from the beginning of the buffer
template <typename T>
size_t RingBuf<T>::pop(size_t numElements) {
  if (!valid()) return 0;
  // Do we have data in the buffer?
  if (size()) {
    // Yes. Is the requested number of elements larger than the used buffer?
    if (numElements >= size()) {
      // Yes. clear the buffer
      size_t n = size();
      clear();
      return n;
    } else {
      LOCK_GUARD(cLock, m);
      // No, the buffer needs to be emptied partly only
      moveFront(numElements);
    }
    return numElements;
  }
  return 0;
}

// moveFront: shift left buffer contents by a given number of elements.
// (used internally only)
template <typename T>
void RingBuf<T>::moveFront(size_t numElements) {
  RB_begin += numElements;
  memmove(RB_buffer, RB_begin, (RB_end - RB_begin) * RB_elementSize);
  RB_begin = RB_buffer;
  RB_end -= numElements;
}

// push_back(single element): add one element to the buffer, potentially discarding previous ones
template <typename T>
bool RingBuf<T>::push_back(const T c) {
  if (!valid()) return false;
  {
    LOCK_GUARD(cLock, m);
    // No more space?
    if (capacity() == 0) {
      // No, we need to drop something
      // Are we to keep the oldest data?
      if (RB_preserve) {
        // Yes. The new element will be dropped to leave the buffer untouched
        return false;
      }
      // We need to drop the oldest element begin is pointing to
      moveFront(1);
    }
    // Now add the element
    *RB_end = c;
    RB_end++;
  }
  return true;
}

// push_back(element buffer): add a batch of elements to the buffer
template <typename T>
bool RingBuf<T>::push_back(const T *data, size_t size) {
  if (!valid()) return false;
  // Do not process nullptr or zero lengths
  if (!data || size == 0) return false;
  // Avoid self-referencing pushes
  if (data >= RB_buffer && data <= (RB_buffer + RB_len)) return false;
  {
    LOCK_GUARD(cLock, m);
    // Is the size to be added fitting the capacity?
    if (size > capacity()) {
      // No. We need to make room first
      // Are we allowed to do that?
      if (RB_preserve) {
        // No. deny the push_back
        return false;
      }
      // Adjust data to the maximum usable size
      if (size > RB_usable) {
        data += (size - RB_usable);
        size = RB_usable;
      }
      // Make room for the data
      moveFront(size - capacity());
    }
    // Now copy it in
    memcpy(RB_end, data, size * RB_elementSize);
    RB_end += size;
  }
  return true;
}

// operator[]: return the element the index is pointing to. If index is
// outside the currently used area, return 0
template <typename T>
const T RingBuf<T>::operator[](size_t index) {
  if (!valid()) return 0;
  if (index >= 0 && index < size()) {
    return *(RB_begin + index);
  }
  return 0;
}

// safeCopy: get a stable data copy from currently used buffer
// target: buffer to copy data into
// len: number of elements requested
// move: if true, copied elements will be pop()-ped
// returns number of elements actually transferred
template <typename T>
size_t RingBuf<T>::safeCopy(T *target, size_t tLen, bool move) {
  if (!valid()) return 0;
  if (!target) return 0;
  {
    LOCK_GUARD(cLock, m);
    if (tLen > size()) tLen = size();
    memcpy(target, RB_begin, tLen * RB_elementSize);
  }
  if (move) pop(tLen);
  return tLen;
}

// Equality: sizes and contents must be identical
template <typename T>
bool RingBuf<T>::operator==(RingBuf<T> &r) {
  if (!valid() || !r.valid()) return false;
  if (size() != r.size()) return false;
  if (memcmp(RB_begin, r.RB_begin, size() * RB_elementSize)) return false;
  return true;
}
#endif
