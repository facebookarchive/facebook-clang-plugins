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

  // Symbols to be stacked
  enum Symbol {
    SARRAY,
    STUPLE,
    SOBJECT,
    SVARIANT,
    STAG
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

    // The next element does not need to be stacked because it is always true (or undef) when we leave a container
    bool lastContainerHasElements_;

    // Objects want tagged values
    static bool needsTag(enum Symbol s) {
      return s == SOBJECT;
    }
    // Variants want at most one value
    static bool canTakeOneValueAtMost(enum Symbol s) {
      return s == SVARIANT;
    }
#endif

    void enterValue() {
#ifdef DEBUG
      if (stack_.empty()) {
        return;
      }
      assert(!needsTag(stack_.back()));
      assert(!canTakeOneValueAtMost(stack_.back()) || !lastContainerHasElements_);
#endif
    }

    void leaveValue() {
#ifdef DEBUG
      if (stack_.empty()) {
        return;
      }
      if (stack_.back() == STAG) {
        stack_.pop_back();
        assert(needsTag(stack_.back()));
      }
      lastContainerHasElements_ = true;
#endif
    }

    void emitValue() {
      enterValue();
      leaveValue();
    }

    void enterContainer(enum Symbol s) {
#ifdef DEBUG
      enterValue();
      stack_.push_back(s);
      lastContainerHasElements_ = false;
#endif
    }

    void leaveContainer(enum Symbol s) {
#ifdef DEBUG
      assert(stack_.back() == s);
      stack_.pop_back();
      leaveValue();
#endif
    }

  public:
    GenWriter(const ATDEmitter &emitter) : emitter_(emitter)
#ifdef DEBUG
    , lastContainerHasElements_(false)
#endif
    {}

    ~GenWriter() {
#ifdef DEBUG
      assert(stack_.empty());
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

    void enterArray() {
      enterContainer(SARRAY);
      emitter_.enterArray();
    }
    void leaveArray() {
      leaveContainer(SARRAY);
      emitter_.leaveArray();
    }
    void enterObject() {
      enterContainer(SOBJECT);
      emitter_.enterObject();
    }
    void leaveObject() {
      leaveContainer(SOBJECT);
      emitter_.leaveObject();
    }
    void enterTuple() {
      enterContainer(STUPLE);
      emitter_.enterTuple();
    }
    void leaveTuple() {
      leaveContainer(STUPLE);
      emitter_.leaveTuple();
    }

    void enterVariant(const std::string &tag) {
      enterContainer(SVARIANT);
      emitter_.enterVariant();
      emitter_.emitVariantTag(tag);
    }
    void leaveVariant() {
      leaveContainer(SVARIANT);
      emitter_.leaveVariant();
    }

    // convenient methods

    void emitSimpleVariant(const std::string &tag) {
      enterVariant(tag);
      leaveVariant();
    }

    void emitFlag(const std::string &tag, bool val) {
      if (val) {
        emitTag(tag);
        emitBoolean(true);
      }
    }

    // convenient classes for automatically closing containers using C++ scoping

#define DECLARE_SCOPE(Name)                                     \
    class Name##Scope {                                         \
      GenWriter &f_;                                            \
    public:                                                     \
    Name##Scope(GenWriter &f) : f_(f) {                         \
        f_.enter##Name();                                       \
      }                                                         \
      ~Name##Scope() {                                          \
        f_.leave##Name();                                       \
      }                                                         \
    };                                                          \

    DECLARE_SCOPE(Array)
    DECLARE_SCOPE(Tuple)
    DECLARE_SCOPE(Object)

    class VariantScope {
      GenWriter &f_;
    public:
      VariantScope(GenWriter &f, const std::string &tag) : f_(f) {
        f_.enterVariant(tag);
      }
      ~VariantScope() {
        f_.leaveVariant();
      }
    };

  };

  // Configure GenWriter for Yojson / Json textual outputs
  template <class OStream, bool standardJson = false>
  class YojsonEmitter {

    const char *QUOTE = "\"";
    const char *COMMA = ",";
    const char *TAB = "  ";
    const char *NEWLINE = "\n";
    const char *COLON = " : ";
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
    unsigned indentLevel_;
    bool nextElementNeedsNewLine_;
    bool previousElementNeedsComma_;
    bool previousElementIsVariantTag_;

  public:
    YojsonEmitter(OStream &os)
    : os_(os),
      indentLevel_(0),
      nextElementNeedsNewLine_(false),
      previousElementNeedsComma_(false),
      previousElementIsVariantTag_(false)
    {}

    void tab() {
      if (previousElementIsVariantTag_) {
        os_ << (standardJson ? COMMAWITHSPACES : COLON);
      } else if (previousElementNeedsComma_) {
        os_ << COMMA;
      }
      if (nextElementNeedsNewLine_) {
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
      os_ << QUOTE << COLON;
      previousElementNeedsComma_ = false;
      nextElementNeedsNewLine_ = false;
      previousElementIsVariantTag_ = false;
    }
    void emitVariantTag(const std::string &val) {
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
    void leaveArray() {
      leaveContainer(RBRACKET);
    }
    void enterObject() {
      enterContainer(LBRACE);
    }
    void leaveObject() {
      leaveContainer(RBRACE);
    }
    void enterTuple() {
      enterContainer(standardJson ? LBRACKET : LPAREN);
    }
    void leaveTuple() {
      leaveContainer(standardJson ? RBRACKET : RPAREN);
    }
    void enterVariant() {
      enterContainer(standardJson ? LBRACKET : LANGLE);
      // cancel indent
      indentLevel_--;
      nextElementNeedsNewLine_ = false;
    }
    void leaveVariant() {
      nextElementNeedsNewLine_ = false;
      leaveContainer(standardJson ? RBRACKET : RANGLE);
      indentLevel_++;
    }

  };

  // The full classes for JSON and YOJSON writing
  template <class OStream, bool standardJson = false>
  class YojsonWriter : public GenWriter<YojsonEmitter<OStream, standardJson>> {
    typedef YojsonEmitter<OStream, standardJson> Emitter;
  public:
    YojsonWriter(OStream &os) : GenWriter<Emitter>(Emitter(os)) {}
  };

  template <class OStream> using JsonWriter=YojsonWriter<OStream, true>;
}
