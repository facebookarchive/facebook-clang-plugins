/**
 * Copyright (c) 2014, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <assert.h>
#include <iostream>
#include <vector>

namespace ATDWriter {

  struct ATDWriterOptions {
    bool useYojson;
    bool prettifyJson;
  };

  // Symbols to be stacked
  enum Symbol {
    SARRAY,
    STUPLE,
    SOBJECT,
    SVARIANT,
    STAG
  };

  // whether the container has a {maximum,exact} size
  enum ContainerSizeKind {
    CSKNONE,  // no size info
    CSKEXACT, // the container expects exactly this number of items
    CSKMAX    // the container expects at most this number of items
  };

  // Main class for writing ATD-like data
  // - In NDEBUG mode this class is only a wrapper around an ATDEmitter
  // - In DEBUG mode it acts as a validator: asserts will fire if the events do not correspond to a well-formed ATD/JSON value
  template <class ATDEmitter>
  class GenWriter {

  protected:
    ATDEmitter emitter_;

  private:
#ifdef DEBUG
    // State of the automaton
    std::vector<enum Symbol> stack_;

    // Objects want tagged values
    static bool needsTag(enum Symbol s) {
      return s == SOBJECT;
    }

    // How many elements are expected in the current container
    std::vector<int> containerSize_;
    std::vector<enum ContainerSizeKind> containerSizeKind_;
#endif

    void enterValue() {
#ifdef DEBUG
      if (stack_.empty()) {
        return;
      }
      assert(!needsTag(stack_.back()));
#endif
    }

    void leaveValue() {
#ifdef DEBUG
      switch (containerSizeKind_.back()) {
      case CSKEXACT:
      case CSKMAX:
        containerSize_.back() -= 1;
        break;
      case CSKNONE:
        break;
      }
      if (stack_.empty()) {
        return;
      }
      if (stack_.back() == STAG) {
        stack_.pop_back();
        assert(needsTag(stack_.back()));
      }
#endif
    }

    void emitValue() {
      enterValue();
      leaveValue();
    }

    void enterContainer(enum Symbol s, enum ContainerSizeKind csk = CSKNONE, int numElems = 0) {
#ifdef DEBUG
      enterValue();
      stack_.push_back(s);
      containerSizeKind_.push_back(csk);
      switch (csk) {
      case CSKEXACT:
      case CSKMAX:
        containerSize_.push_back(numElems);
        break;
      case CSKNONE:
        break;
      }
#endif
    }

    void leaveContainer(enum Symbol s) {
#ifdef DEBUG
      assert(stack_.back() == s);
      stack_.pop_back();
      switch (containerSizeKind_.back()) {
      case CSKEXACT:
        assert(containerSize_.back() == 0);
      case CSKMAX:
        assert(!containerSize_.empty());
        assert(containerSize_.back() >= 0);
        containerSize_.pop_back();
        break;
      case CSKNONE:
        break;
      }
      containerSizeKind_.pop_back();
      leaveValue();
#endif
    }

  public:
    GenWriter(const ATDEmitter &emitter) : emitter_(emitter)
    {
#ifdef DEBUG
      containerSizeKind_.push_back(CSKNONE);
#endif
    }

    ~GenWriter() {
#ifdef DEBUG
      assert(stack_.empty());
      assert(containerSizeKind_.size() == 1);
      assert(containerSizeKind_.back() == CSKNONE);
#endif
      emitter_.emitEOF();
    }

    void emitNull() {
      emitValue();
      emitter_.emitNull();
    }
    void emitBoolean(bool val) {
      emitValue();
      emitter_.emitBoolean(val);
    }
    void emitInteger(int val) {
      emitValue();
      emitter_.emitInteger(val);
    }
    void emitFloat(float val) {
      emitValue();
      emitter_.emitFloat(val);
    }
    void emitString(const std::string &val) {
      emitValue();
      emitter_.emitString(val);
    }
    void emitTag(const std::string &val) {
#ifdef DEBUG
      assert(needsTag(stack_.back()));
      stack_.push_back(STAG);
#endif
      emitter_.emitTag(val);
    }

    void enterArray(int numElems) {
      enterContainer(SARRAY, CSKEXACT, numElems);
      emitter_.enterArray(numElems);
    }
    void enterArray() {
      enterContainer(SARRAY);
      emitter_.enterArray();
    }
    void leaveArray() {
      leaveContainer(SARRAY);
      emitter_.leaveArray();
    }
    void enterObject(int numElems) {
      enterContainer(SOBJECT, CSKMAX, numElems);
      emitter_.enterObject(numElems);
    }
    void enterObject() {
      enterContainer(SOBJECT);
      emitter_.enterObject();
    }
    void leaveObject() {
      leaveContainer(SOBJECT);
      emitter_.leaveObject();
    }
    void enterTuple(int numElems) {
      enterContainer(STUPLE, CSKEXACT, numElems);
      emitter_.enterTuple(numElems);
    }
    void enterTuple() {
      enterContainer(STUPLE);
      emitter_.enterTuple();
    }
    void leaveTuple() {
      leaveContainer(STUPLE);
      emitter_.leaveTuple();
    }

    void enterVariant(const std::string &tag, bool hasArg = true) {
      // variants have at most one value, so we can safely use hasArg
      // as the number of arguments
      enterContainer(SVARIANT, CSKEXACT, hasArg);
      emitter_.enterVariant();
      emitter_.emitVariantTag(tag, hasArg);
    }
    void leaveVariant() {
      leaveContainer(SVARIANT);
      emitter_.leaveVariant();
    }
    void emitSimpleVariant(const std::string &tag) {
      if (emitter_.shouldSimpleVariantsBeEmittedAsStrings) {
        emitString(tag);
      } else {
        enterVariant(tag, false);
        leaveVariant();
      }
    }

    // convenient methods

    void emitFlag(const std::string &tag, bool val) {
      if (val) {
        emitTag(tag);
        emitBoolean(true);
      }
    }

    // convenient classes for automatically closing containers using C++ scoping

    class ArrayScope {
      GenWriter &f_;
    public:
      ArrayScope(GenWriter &f, int size) : f_(f) {
        f_.enterArray(size);
      }
      ArrayScope(GenWriter &f) : f_(f) {
        f_.enterArray();
      }
      ~ArrayScope() {
        f_.leaveArray();
      }
    };

    class ObjectScope {
      GenWriter &f_;
    public:
      ObjectScope(GenWriter &f, int size) : f_(f) {
        f_.enterObject(size);
      }
      ObjectScope(GenWriter &f) : f_(f) {
        f_.enterObject();
      }
      ~ObjectScope() {
        f_.leaveObject();
      }
    };

    class TupleScope {
      GenWriter &f_;
    public:
      TupleScope(GenWriter &f, int size) : f_(f) {
        f_.enterTuple(size);
      }
      TupleScope(GenWriter &f) : f_(f) {
        f_.enterTuple();
      }
      ~TupleScope() {
        f_.leaveTuple();
      }
    };

    class VariantScope {
      GenWriter &f_;
    public:
      VariantScope(GenWriter &f, const std::string &tag) : f_(f) {
        f_.enterVariant(tag, true);
      }
      ~VariantScope() {
        f_.leaveVariant();
      }
    };

  };

  // Configure GenWriter for Yojson / Json textual outputs
  template <class OStream>
  class JsonEmitter {

    const char *QUOTE = "\"";
    const char *COMMA = ",";
    const char *TAB = "  ";
    const char *NEWLINE = "\n";
    const char *COLON = ":";
    const char *COLONWITHSPACES = " : ";
    const char *COMMAWITHSPACES = " , ";
    const char *NULLSTR = "null";
    const char *FALSESTR = "false";
    const char *TRUESTR = "true";
    const char LBRACKET = '[';
    const char RBRACKET = ']';
    const char LBRACE = '{';
    const char RBRACE = '}';
    const char LPAREN = '(';
    const char RPAREN = ')';
    const char LANGLE = '<';
    const char RANGLE = '>';

  private:
    OStream &os_;
    const ATDWriterOptions options_;
    unsigned indentLevel_;
    bool nextElementNeedsNewLine_;
    bool previousElementNeedsComma_;
    bool previousElementIsVariantTag_;

  public:
    bool shouldSimpleVariantsBeEmittedAsStrings;

    JsonEmitter(OStream &os, const ATDWriterOptions opts)
    : os_(os),
      options_(opts),
      indentLevel_(0),
      nextElementNeedsNewLine_(false),
      previousElementNeedsComma_(false),
      previousElementIsVariantTag_(false),
      shouldSimpleVariantsBeEmittedAsStrings(!opts.useYojson)
    {}

    void tab() {
      if (previousElementIsVariantTag_) {
        if (options_.prettifyJson) {
          os_ << (options_.useYojson ? COLONWITHSPACES : COMMAWITHSPACES);
        } else {
          os_ << (options_.useYojson ? COLON : COMMA);
        }
      } else if (previousElementNeedsComma_) {
        os_ << COMMA;
      }
      if (nextElementNeedsNewLine_ && options_.prettifyJson) {
        os_ << NEWLINE;
        for (size_t i = 0; i < indentLevel_; i++) {
          os_ << TAB;
        }
      }
    }

  private:
    // TODO: unicode and other control chars
    void write_escaped(const std::string &val) {
      for (std::string::const_iterator i = val.begin(), e = val.end(); i != e; i++) {
        char x = *i;
        switch (x) {
          case '\\': os_ << "\\\\"; break;
          case '"': os_ << "\\\""; break;
          case '\n': os_ << "\\n"; break;
          case '\t': os_ << "\\t"; break;
          case '\b': os_ << "\\b"; break;
          default: os_ << x; break;
        }
      }
    }

    void enterContainer(char c) {
      tab();
      os_ << c;
      indentLevel_++;
      previousElementNeedsComma_ = false;
      nextElementNeedsNewLine_ = true;
      previousElementIsVariantTag_ = false;
    }

    void leaveContainer(char c) {
      indentLevel_--;
      // suppress the last comma or variant separator
      previousElementNeedsComma_ = false;
      previousElementIsVariantTag_ = false;
      tab();
      os_ << c;
      previousElementNeedsComma_ = true;
      nextElementNeedsNewLine_ = true;
    }

  public:

    void emitEOF() {
      os_ << NEWLINE;
    }

    void emitNull() {
      tab();
      os_ << NULLSTR;
      previousElementNeedsComma_ = true;
      nextElementNeedsNewLine_ = true;
      previousElementIsVariantTag_ = false;
    }
    void emitBoolean(bool val) {
      tab();
      os_ << (val ? TRUESTR : FALSESTR);
      previousElementNeedsComma_ = true;
      nextElementNeedsNewLine_ = true;
      previousElementIsVariantTag_ = false;
    }
    void emitInteger(unsigned val) {
      tab();
      os_ << val;
      previousElementNeedsComma_ = true;
      nextElementNeedsNewLine_ = true;
      previousElementIsVariantTag_ = false;
    }
    void emitString(const std::string &val) {
      tab();
      os_ << QUOTE;
      write_escaped(val);
      os_ << QUOTE;
      previousElementNeedsComma_ = true;
      nextElementNeedsNewLine_ = true;
      previousElementIsVariantTag_ = false;
    }
    void emitTag(const std::string &val) {
      tab();
      os_ << QUOTE;
      write_escaped(val);
      os_ << QUOTE;
      if (options_.prettifyJson) {
        os_ << COLONWITHSPACES;
      } else {
        os_ << COLON;
      }
      previousElementNeedsComma_ = false;
      nextElementNeedsNewLine_ = false;
      previousElementIsVariantTag_ = false;
    }
    void emitVariantTag(const std::string &val, bool hasArgs) {
      tab();
      os_ << QUOTE;
      write_escaped(val);
      os_ << QUOTE;
      previousElementNeedsComma_ = false;
      nextElementNeedsNewLine_ = false;
      previousElementIsVariantTag_ = true;
    }

    void enterArray() {
      enterContainer(LBRACKET);
    }
    void enterArray(int size) {
      enterArray();
    }
    void leaveArray() {
      leaveContainer(RBRACKET);
    }
    void enterObject() {
      enterContainer(LBRACE);
    }
    void enterObject(int size) {
      enterObject();
    }
    void leaveObject() {
      leaveContainer(RBRACE);
    }
    void enterTuple() {
      enterContainer(options_.useYojson ? LPAREN : LBRACKET);
    }
    void enterTuple(int size) {
      enterTuple();
    }
    void leaveTuple() {
      leaveContainer(options_.useYojson ? RPAREN : RBRACKET);
    }
    void enterVariant() {
      enterContainer(options_.useYojson ? LANGLE : LBRACKET);
      // cancel indent
      indentLevel_--;
      nextElementNeedsNewLine_ = false;
    }
    void leaveVariant() {
      nextElementNeedsNewLine_ = false;
      leaveContainer(options_.useYojson ? RANGLE : RBRACKET);
      indentLevel_++;
    }

  };

  // Configure GenWriter for Biniou binary output
  template <class OStream>
  class BiniouEmitter {

  private:
    OStream &os_;

    const uint8_t bool_tag = 0;
    const uint8_t int8_tag = 1;
    const uint8_t int16_tag = 2;
    const uint8_t int32_tag = 3;
    const uint8_t int64_tag = 4;
    const uint8_t float64_tag = 12;
    const uint8_t uvint_tag = 16;
    const uint8_t svint_tag = 17;
    const uint8_t string_tag = 18;
    const uint8_t ARRAY_tag = 19;
    const uint8_t TUPLE_tag = 20;
    const uint8_t RECORD_tag = 21;
    const uint8_t NUM_VARIANT_tag = 22;
    const uint8_t VARIANT_tag = 23;
    const uint8_t unit_tag = 24;
    const uint8_t TABLE_tag = 25;
    const uint8_t SHARED_tag = 26;

    // How many elements do we expect at most for the currently opened RECORDs?
    // Records must remember how many items are supposed to be in the record.
    // If a record is closed when not all items have been emitted then the
    // remaining number of dummy items are emitted to make biniou happy.
    // This is because, thanks to optional fields and fields with default arguments,
    // it can be hard to precisely estimate how many items of the record are
    // actually going to be emitted in advance.
    std::vector<int> recordMaxSize_;
    // Are we the first element of an array?
    // This is needed because arrays are monomorphic and only the first element
    // of the array carries a value tag.
    bool isFirstInArray_;
    // Are we currently in a record/an array?
    std::vector<bool> isCurrentValueInRecord_;
    std::vector<bool> isCurrentValueInArray_;

  public:
    bool shouldSimpleVariantsBeEmittedAsStrings = false;

    BiniouEmitter(OStream &os)
    : os_(os)
    {
      isCurrentValueInRecord_.push_back(false);
      isCurrentValueInArray_.push_back(false);
    }

  private:
    void enterContainer(uint8_t tag, int size) {
      writeValueTag(tag);
      if (size >= 0) {
        writeUvint(size);
      }

      bool isRecord = tag == RECORD_tag;
      isCurrentValueInRecord_.push_back(isRecord);
      bool isArray = tag == ARRAY_tag;
      isCurrentValueInArray_.push_back(isArray);

      // extra initialization for some containers
      if (isArray) {
        isFirstInArray_ = true;
      }
      if (isRecord) {
        recordMaxSize_.push_back(size);
      }
    }

    void leaveValue() {
      if (isCurrentValueInRecord_.back()) {
        recordMaxSize_.back() -= 1;
      }
      isFirstInArray_ = false;
    }

    void leaveContainer() {
      isCurrentValueInRecord_.pop_back();
      isCurrentValueInArray_.pop_back();
      leaveValue();
    }

    // string hash algorithm from the biniou spec
    uint32_t biniou_hash(const std::string &str) {
      uint32_t hash = 0;
      for (const char &c : str) {
        hash = 223 * hash + c;
      }
      hash %= 1 << 31;
      return hash;
    }

    void write8(uint8_t c) {
      os_.write((const char *)&c, 1);
    }

    void write32(int32_t x) {
      write8(x >> 24);
      write8(x >> 16);
      write8(x >> 8);
      write8(x);
    }

    void write64(int64_t x) {
      write32(x >> 32);
      write32(x);
    }

    void writeUvint(size_t x) {
      while (x > 127) {
        write8(x | 128);
        x >>= 7;
      }

      write8((uint8_t) x);
    }

    void writeSvint(int x) {
      if (x >= 0) {
        writeUvint(x * 2);
      } else {
        writeUvint(-x * 2 - 1);
      }
    }

    void writeValueTag(uint8_t tag) {
      if (!isCurrentValueInArray_.back() || isFirstInArray_) {
        write8(tag);
      }
    }

    void emitDummyRecordField() {
      emitTag("!!DUMMY!!");
      // unit is the smallest value (2 bytes)
      write8(unit_tag);
      write8(0);
    }

  public:
    void emitEOF() { }

    void emitBoolean(bool val) {
      writeValueTag(bool_tag);
      write8(val);
      leaveValue();
    }

    void emitInteger(int val) {
      writeValueTag(svint_tag);
      writeSvint(val);
      leaveValue();
    }

    void emitString(const std::string &val) {
      writeValueTag(string_tag);
      writeUvint(val.length());
      for(const char &c: val) {
        write8(c);
      }
      leaveValue();
    }

    void emitTag(const std::string &val) {
      int32_t hash = biniou_hash(val);
      // set first bit of hash
      hash |= 1 << 31;
      write32(hash);
    }

    void emitVariantTag(const std::string &val, bool hasArg) {
      int32_t hash = biniou_hash(val);
      // set first bit of hash if the variant has an argument
      if (hasArg) {
        hash |= 1 << 31;
      }
      write32(hash);
    }

    void enterArray(int size) {
      enterContainer(ARRAY_tag, size);
    }
    void leaveArray() {
      leaveContainer();
    }
    void enterObject(int size) {
      enterContainer(RECORD_tag, size);
    }
    void leaveObject() {
      for (int i = recordMaxSize_.back(); i > 0; --i) {
        emitDummyRecordField();
      }
      recordMaxSize_.pop_back();
      leaveContainer();
    }
    void enterTuple(int size) {
      enterContainer(TUPLE_tag, size);
    }
    void leaveTuple() {
      leaveContainer();
    }
    void enterVariant() {
      enterContainer(VARIANT_tag, -1);
    }
    void leaveVariant() {
      leaveContainer();
    }

  };

  // The full class for JSON and YOJSON writing
  template <class OStream>
  class JsonWriter : public GenWriter<JsonEmitter<OStream>> {
    typedef JsonEmitter<OStream> Emitter;
  public:
    JsonWriter(OStream &os, const ATDWriterOptions opts)
      : GenWriter<Emitter>(Emitter(os, opts))
      {}
  };

  // The full class for biniou writing
  template <class OStream>
  class BiniouWriter : public GenWriter<BiniouEmitter<OStream>> {
    typedef BiniouEmitter<OStream> Emitter;
  public:
    BiniouWriter(OStream &os)
      : GenWriter<Emitter>(Emitter(os))
      {}

    BiniouWriter(OStream &os, const ATDWriterOptions opts)
      : GenWriter<Emitter>(Emitter(os))
      {}
  };

}
