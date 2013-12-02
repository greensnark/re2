/*
 * re2 (http://github.com/mudge/re2)
 * Ruby bindings to re2, an "efficient, principled regular expression library"
 *
 * Copyright (c) 2010-2013, Paul Mucur (http://mudge.name)
 * Released under the BSD Licence, please see LICENSE.txt
 */

#include <re2/re2.h>
#include <ruby.h>
#include <string>
#include <sstream>
#include <vector>
using std::string;
using std::ostringstream;
using std::nothrow;
using std::map;
using std::vector;

extern "C" {
  #ifdef HAVE_RUBY_ENCODING_H
    #include <ruby/encoding.h>
    #define ENCODED_STR_NEW(str, length, encoding) \
      ({ \
        VALUE _string = rb_str_new(str, length); \
        int _enc = rb_enc_find_index(encoding); \
        rb_enc_associate_index(_string, _enc); \
        _string; \
      })
  #else
    #define ENCODED_STR_NEW(str, length, encoding) \
      rb_str_new((const char *)str, (long)length)
  #endif

  #define BOOL2RUBY(v) (v ? Qtrue : Qfalse)
  #define UNUSED(x) ((void)x)

  #ifndef RSTRING_LEN
    #define RSTRING_LEN(x) (RSTRING(x)->len)
  #endif

  #ifndef RSTRING_PTR
    #define RSTRING_PTR(x) (RSTRING(x)->ptr)
  #endif

  #ifdef HAVE_ENDPOS_ARGUMENT
    #define match(pattern, text, startpos, endpos, anchor, match, nmatch) \
            (pattern->Match(text, startpos, endpos, anchor, match, nmatch))
  #else
    #define match(pattern, text, startpos, endpos, anchor, match, nmatch) \
            (pattern->Match(text, startpos, anchor, match, nmatch))
  #endif

  typedef struct {
    RE2 *pattern;
  } re2_pattern;

  typedef struct {
    re2::StringPiece *matches;
    int number_of_matches;
    VALUE regexp, text;
  } re2_matchdata;

  typedef struct {
    re2::StringPiece *input;
    int number_of_capturing_groups;
    VALUE regexp, text;
  } re2_consumer;

  VALUE re2_mRE2, re2_cRegexp, re2_cMatchData, re2_cConsumer;

  /* Symbols used in RE2 options. */
  static ID id_utf8, id_posix_syntax, id_longest_match, id_log_errors,
            id_max_mem, id_literal, id_never_nl, id_case_sensitive,
            id_perl_classes, id_word_boundary, id_one_line;

  void re2_matchdata_mark(re2_matchdata* self) {
    rb_gc_mark(self->regexp);
    rb_gc_mark(self->text);
  }

  void re2_matchdata_free(re2_matchdata* self) {
    if (self->matches) {
      delete[] self->matches;
    }
    free(self);
  }

  void re2_consumer_mark(re2_consumer* self) {
    rb_gc_mark(self->regexp);
    rb_gc_mark(self->text);
  }

  void re2_consumer_free(re2_consumer* self) {
    if (self->input) {
      delete self->input;
    }
    free(self);
  }

  void re2_regexp_free(re2_pattern* self) {
    if (self->pattern) {
      delete self->pattern;
    }
    free(self);
  }

  static VALUE re2_matchdata_allocate(VALUE klass) {
    re2_matchdata *m;
    return Data_Make_Struct(klass, re2_matchdata, re2_matchdata_mark,
        re2_matchdata_free, m);
  }

  static VALUE re2_consumer_allocate(VALUE klass) {
    re2_consumer *c;
    return Data_Make_Struct(klass, re2_consumer, re2_consumer_mark,
        re2_consumer_free, c);
  }

  /*
   * Returns a frozen copy of the string passed into +match+.
   *
   * @return [String] a frozen copy of the passed string.
   * @example
   *   m = RE2::Regexp.new('(\d+)').match("bob 123")
   *   m.string  #=> "bob 123"
   */
  static VALUE re2_matchdata_string(VALUE self) {
    re2_matchdata *m;
    Data_Get_Struct(self, re2_matchdata, m);

    return m->text;
  }

  /*
   * Returns the string passed into the consumer.
   *
   * @return [String] the original string.
   * @example
   *   c = RE2::Regexp.new('(\d+)').consume("foo")
   *   c.string #=> "foo"
   */
  static VALUE re2_consumer_string(VALUE self) {
    re2_consumer *c;
    Data_Get_Struct(self, re2_consumer, c);

    return c->text;
  }

  /*
   * Rewind the consumer to the start of the string.
   *
   * @example
   *   c = RE2::Regexp.new('(\d+)').consume("1 2 3")
   *   e = c.to_enum
   *   e.next #=> ["1"]
   *   e.next #=> ["2"]
   *   c.rewind
   *   e.next #=> ["1"]
   */
  static VALUE re2_consumer_rewind(VALUE self) {
    re2_consumer *c;
    Data_Get_Struct(self, re2_consumer, c);

    c->input = new(nothrow) re2::StringPiece(StringValuePtr(c->text));

    return self;
  }

  /*
   * Scan the given text incrementally for matches, returning an array of
   * matches on each subsequent call. Returns nil if no matches are found.
   *
   * @return [Array<String>] the matches.
   * @example
   *   c = RE2::Regexp.new('(\w+)').consume("Foo bar baz")
   *   c.consume #=> ["Foo"]
   *   c.consume #=> ["bar"]
   */
  static VALUE re2_consumer_consume(VALUE self) {
    int i;
    re2_pattern *p;
    re2_consumer *c;
    VALUE result;

    Data_Get_Struct(self, re2_consumer, c);
    Data_Get_Struct(c->regexp, re2_pattern, p);

    vector<RE2::Arg> argv(c->number_of_capturing_groups);
    vector<RE2::Arg*> args(c->number_of_capturing_groups);
    vector<string> matches(c->number_of_capturing_groups);

    for (i = 0; i < c->number_of_capturing_groups; i++) {
      matches[i] = "";
      argv[i] = &matches[i];
      args[i] = &argv[i];
    }

    if (RE2::FindAndConsumeN(c->input, *p->pattern, &args[0],
          c->number_of_capturing_groups)) {
      result = rb_ary_new2(c->number_of_capturing_groups);
      for (i = 0; i < c->number_of_capturing_groups; i++) {
        if (matches[i].empty()) {
          rb_ary_push(result, Qnil);
        } else {
          rb_ary_push(result, ENCODED_STR_NEW(matches[i].data(),
                matches[i].size(),
                p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1"));
        }
      }
    } else {
      result = Qnil;
    }

    return result;
  }

  /*
   * Returns the number of elements in the match array (including nils).
   *
   * @return [Fixnum] the number of elements
   * @example
   *   m = RE2::Regexp.new('(\d+)').match("bob 123")
   *   m.size      #=> 2
   *   m.length    #=> 2
   */
  static VALUE re2_matchdata_size(VALUE self) {
    re2_matchdata *m;
    Data_Get_Struct(self, re2_matchdata, m);

    return INT2FIX(m->number_of_matches);
  }

  /*
   * Returns the {RE2::Regexp} used in the match.
   *
   * @return [RE2::Regexp] the regexp used in the match
   * @example
   *   m = RE2::Regexp.new('(\d+)').match("bob 123")
   *   m.regexp    #=> #<RE2::Regexp /(\d+)/>
   */
  static VALUE re2_matchdata_regexp(VALUE self) {
    re2_matchdata *m;
    Data_Get_Struct(self, re2_matchdata, m);
    return m->regexp;
  }

  /*
   * Returns the {RE2::Regexp} used in the consumer.
   *
   * @return [RE2::Regexp] the regexp used in the consumer
   * @example
   *   c = RE2::Regexp.new('(\d+)').consume("bob 123")
   *   c.regexp    #=> #<RE2::Regexp /(\d+)/>
   */
  static VALUE re2_consumer_regexp(VALUE self) {
    re2_consumer *c;
    Data_Get_Struct(self, re2_consumer, c);

    return c->regexp;
  }

  static VALUE re2_regexp_allocate(VALUE klass) {
    re2_pattern *p;
    return Data_Make_Struct(klass, re2_pattern, 0, re2_regexp_free, p);
  }

  /*
   * Returns the array of matches.
   *
   * @return [Array<String, nil>] the array of matches
   * @example
   *   m = RE2::Regexp.new('(\d+)').match("bob 123")
   *   m.to_a    #=> ["123", "123"]
   */
  static VALUE re2_matchdata_to_a(VALUE self) {
    int i;
    re2_matchdata *m;
    re2_pattern *p;
    re2::StringPiece match;
    VALUE array;

    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    array = rb_ary_new2(m->number_of_matches);
    for (i = 0; i < m->number_of_matches; i++) {
      if (m->matches[i].empty()) {
        rb_ary_push(array, Qnil);
      } else {
        match = m->matches[i];
        rb_ary_push(array, ENCODED_STR_NEW(match.data(), match.size(),
              p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1"));
      }
    }

    return array;
  }

  static re2::StringPiece *
  re2_matchdata_nth_stringpiece(int nth, VALUE self) {
    re2_matchdata *m;
    re2_pattern *p;
    re2::StringPiece match;

    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    if (nth < 0 || nth >= m->number_of_matches) {
      return NULL;
    } else {
      return m->matches + nth;
    }
  }

  static re2::StringPiece *
  re2_matchdata_named_stringpiece(const char* name, VALUE self)
  {
    int idx;
    re2_matchdata *m;
    re2_pattern *p;
    map<string, int> groups;
    string name_as_string(name);

    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    groups = p->pattern->NamedCapturingGroups();

    if (groups.count(name_as_string) == 1) {
      idx = groups[name_as_string];
      return re2_matchdata_nth_stringpiece(idx, self);
    } else {
      return NULL;
    }
  }

  static re2::StringPiece *re2_matchdata_stringpiece_at(VALUE idx, VALUE self) {
    if (TYPE(idx) == T_STRING) {
      return re2_matchdata_named_stringpiece(StringValuePtr(idx), self);
    } else if (TYPE(idx) == T_SYMBOL) {
      return re2_matchdata_named_stringpiece(rb_id2name(SYM2ID(idx)), self);
    } else if (FIXNUM_P(idx)) {
      return re2_matchdata_nth_stringpiece(FIX2INT(idx), self);
    }
    return NULL;
  }

  static VALUE re2_matchdata_nth_match(int nth, VALUE self) {
    re2::StringPiece *match = re2_matchdata_nth_stringpiece(nth, self);
    re2_matchdata *m;
    re2_pattern *p;

    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    if (!match || match->empty()) {
      return Qnil;
    } else {
      return ENCODED_STR_NEW(match->data(), match->size(),
            p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");
    }
  }

  static VALUE re2_matchdata_named_match(const char* name, VALUE self) {
    re2_matchdata *m;
    re2_pattern *p;
    re2::StringPiece *match = re2_matchdata_named_stringpiece(name, self);

    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    if (!match) {
      return Qnil;
    } else {
      return ENCODED_STR_NEW(match->data(), match->size(),
            p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");
    }
  }

  /*
   * Retrieve zero, one or more matches by index or name.
   *
   * @return [Array<String, nil>, String, Boolean]
   *
   * @overload [](index)
   *   Access a particular match by index.
   *
   *   @param [Fixnum] index the index of the match to fetch
   *   @return [String, nil] the specified match
   *   @example
   *     m = RE2::Regexp.new('(\d+)').match("bob 123")
   *     m[0]    #=> "123"
   *
   * @overload [](start, length)
   *   Access a range of matches by starting index and length.
   *
   *   @param [Fixnum] start the index from which to start
   *   @param [Fixnum] length the number of elements to fetch
   *   @return [Array<String, nil>] the specified matches
   *   @example
   *     m = RE2::Regexp.new('(\d+)').match("bob 123")
   *     m[0, 1]    #=> ["123"]
   *
   * @overload [](range)
   *   Access a range of matches by index.
   *
   *   @param [Range] range the range of match indexes to fetch
   *   @return [Array<String, nil>] the specified matches
   *   @example
   *     m = RE2::Regexp.new('(\d+)').match("bob 123")
   *     m[0..1]    #=> "[123", "123"]
   *
   * @overload [](name)
   *   Access a particular match by name.
   *
   *   @param [String, Symbol] name the name of the match to fetch
   *   @return [String, nil] the specific match
   *   @example
   *     m = RE2::Regexp.new('(?P<number>\d+)').match("bob 123")
   *     m["number"] #=> "123"
   *     m[:number]  #=> "123"
   */
  static VALUE re2_matchdata_aref(int argc, VALUE *argv, VALUE self) {
    VALUE idx, rest;
    rb_scan_args(argc, argv, "11", &idx, &rest);

    if (TYPE(idx) == T_STRING) {
      return re2_matchdata_named_match(StringValuePtr(idx), self);
    } else if (TYPE(idx) == T_SYMBOL) {
      return re2_matchdata_named_match(rb_id2name(SYM2ID(idx)), self);
    } else if (!NIL_P(rest) || !FIXNUM_P(idx) || FIX2INT(idx) < 0) {
      return rb_ary_aref(argc, argv, re2_matchdata_to_a(self));
    } else {
      return re2_matchdata_nth_match(FIX2INT(idx), self);
    }
  }

  static VALUE re2_matchdata_begin(int argc, VALUE *argv, VALUE self) {
    VALUE idx;
    rb_scan_args(argc, argv, "01", &idx);
    idx = NIL_P(idx) ? INT2FIX(0) : idx;
    re2::StringPiece *match = re2_matchdata_stringpiece_at(idx, self);
    if (!match) {
      return Qnil;
    }

    re2_matchdata *m = NULL;
    re2_pattern *p = NULL;
    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    // Calculating the position is non-trivial, since the position is
    // the number of characters from the start of the text, not octets:
    const char *text = StringValuePtr(m->text);
    const size_t length = match->data() - text;
    VALUE pre_match = ENCODED_STR_NEW(text, length,
                      p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");
    VALUE res = LONG2FIX(rb_str_strlen(pre_match));
    rb_str_free(pre_match);
    return res;
  }

  static VALUE re2_matchdata_end(int argc, VALUE *argv, VALUE self) {
    VALUE idx;
    rb_scan_args(argc, argv, "01", &idx);
    idx = NIL_P(idx) ? INT2FIX(0) : idx;
    re2::StringPiece *match = re2_matchdata_stringpiece_at(idx, self);
    if (!match) {
      return Qnil;
    }

    re2_matchdata *m;
    re2_pattern *p;
    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    // Calculating the position is non-trivial, since the position is
    // the number of characters from the start of the text, not octets:
    const char *text = StringValuePtr(m->text);
    const size_t length = match->data() - text;
    VALUE pre_match = ENCODED_STR_NEW(text, length + match->length(),
                      p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");
    VALUE res = LONG2FIX(rb_str_strlen(pre_match));
    rb_str_free(pre_match);
    return res;
  }

  /*
   * Returns the entire matched string.
   *
   * @return [String] the entire matched string
   */
  static VALUE re2_matchdata_to_s(VALUE self) {
    return re2_matchdata_nth_match(0, self);
  }

  /*
   * Returns a printable version of the match.
   *
   * @return [String] a printable version of the match
   * @example
   *   m = RE2::Regexp.new('(\d+)').match("bob 123")
   *   m.inspect    #=> "#<RE2::MatchData \"123\" 1:\"123\">"
   */
  static VALUE re2_matchdata_inspect(VALUE self) {
    int i;
    re2_matchdata *m;
    re2_pattern *p;
    VALUE match, result;
    ostringstream output;

    Data_Get_Struct(self, re2_matchdata, m);
    Data_Get_Struct(m->regexp, re2_pattern, p);

    output << "#<RE2::MatchData";

    for (i = 0; i < m->number_of_matches; i++) {
      output << " ";

      if (i > 0) {
        output << i << ":";
      }

      match = re2_matchdata_nth_match(i, self);

      if (match == Qnil) {
        output << "nil";
      } else {
        output << "\"" << StringValuePtr(match) << "\"";
      }
    }

    output << ">";

    result = ENCODED_STR_NEW(output.str().data(), output.str().length(),
        p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");

    return result;
  }

  /*
   * Returns a new RE2 object with a compiled version of
   * +pattern+ stored inside. Equivalent to +RE2.new+.
   *
   * @return [RE2::Regexp] an RE2::Regexp with the specified pattern and options
   * @param [String] pattern the pattern to compile
   * @param [Hash] options the options to compile a regexp with
   * @see RE2::Regexp.new
   *
   */
  static VALUE re2_re2(int argc, VALUE *argv, VALUE self) {
    UNUSED(self);
    return rb_class_new_instance(argc, argv, re2_cRegexp);
  }

  /*
   * Returns a new {RE2::Regexp} object with a compiled version of
   * +pattern+ stored inside.
   *
   * @return [RE2::Regexp]
   *
   * @overload initialize(pattern)
   *   Returns a new {RE2::Regexp} object with a compiled version of
   *   +pattern+ stored inside with the default options.
   *
   *   @param [String] pattern the pattern to compile
   *   @return [RE2::Regexp] an RE2::Regexp with the specified pattern
   *   @raise [NoMemoryError] if memory could not be allocated for the compiled
   *                          pattern
   *
   * @overload initialize(pattern, options)
   *   Returns a new {RE2::Regexp} object with a compiled version of
   *   +pattern+ stored inside with the specified options.
   *
   *   @param [String] pattern the pattern to compile
   *   @param [Hash] options the options with which to compile the pattern
   *   @option options [Boolean] :utf8 (true) text and pattern are UTF-8; otherwise Latin-1
   *   @option options [Boolean] :posix_syntax (false) restrict regexps to POSIX egrep syntax
   *   @option options [Boolean] :longest_match (false) search for longest match, not first match
   *   @option options [Boolean] :log_errors (true) log syntax and execution errors to ERROR
   *   @option options [Fixnum] :max_mem approx. max memory footprint of RE2
   *   @option options [Boolean] :literal (false) interpret string as literal, not regexp
   *   @option options [Boolean] :never_nl (false) never match \n, even if it is in regexp
   *   @option options [Boolean] :case_sensitive (true) match is case-sensitive (regexp can override with (?i) unless in posix_syntax mode)
   *   @option options [Boolean] :perl_classes (false) allow Perl's \d \s \w \D \S \W when in posix_syntax mode
   *   @option options [Boolean] :word_boundary (false) allow \b \B (word boundary and not) when in posix_syntax mode
   *   @option options [Boolean] :one_line (false) ^ and $ only match beginning and end of text when in posix_syntax mode
   *   @return [RE2::Regexp] an RE2::Regexp with the specified pattern and options
   *   @raise [NoMemoryError] if memory could not be allocated for the compiled pattern
   */
  static VALUE re2_regexp_initialize(int argc, VALUE *argv, VALUE self) {
    VALUE pattern, options, utf8, posix_syntax, longest_match, log_errors,
          max_mem, literal, never_nl, case_sensitive, perl_classes,
          word_boundary, one_line;
    re2_pattern *p;

    rb_scan_args(argc, argv, "11", &pattern, &options);
    Data_Get_Struct(self, re2_pattern, p);

    if (RTEST(options)) {
      if (TYPE(options) != T_HASH) {
        rb_raise(rb_eArgError, "options should be a hash");
      }

      RE2::Options re2_options;

      utf8 = rb_hash_aref(options, ID2SYM(id_utf8));
      if (!NIL_P(utf8)) {
        re2_options.set_utf8(RTEST(utf8));
      }

      posix_syntax = rb_hash_aref(options, ID2SYM(id_posix_syntax));
      if (!NIL_P(posix_syntax)) {
        re2_options.set_posix_syntax(RTEST(posix_syntax));
      }

      longest_match = rb_hash_aref(options, ID2SYM(id_longest_match));
      if (!NIL_P(longest_match)) {
        re2_options.set_longest_match(RTEST(longest_match));
      }

      log_errors = rb_hash_aref(options, ID2SYM(id_log_errors));
      if (!NIL_P(log_errors)) {
        re2_options.set_log_errors(RTEST(log_errors));
      }

      max_mem = rb_hash_aref(options, ID2SYM(id_max_mem));
      if (!NIL_P(max_mem)) {
        re2_options.set_max_mem(NUM2INT(max_mem));
      }

      literal = rb_hash_aref(options, ID2SYM(id_literal));
      if (!NIL_P(literal)) {
        re2_options.set_literal(RTEST(literal));
      }

      never_nl = rb_hash_aref(options, ID2SYM(id_never_nl));
      if (!NIL_P(never_nl)) {
        re2_options.set_never_nl(RTEST(never_nl));
      }

      case_sensitive = rb_hash_aref(options, ID2SYM(id_case_sensitive));
      if (!NIL_P(case_sensitive)) {
        re2_options.set_case_sensitive(RTEST(case_sensitive));
      }

      perl_classes = rb_hash_aref(options, ID2SYM(id_perl_classes));
      if (!NIL_P(perl_classes)) {
        re2_options.set_perl_classes(RTEST(perl_classes));
      }

      word_boundary = rb_hash_aref(options, ID2SYM(id_word_boundary));
      if (!NIL_P(word_boundary)) {
        re2_options.set_word_boundary(RTEST(word_boundary));
      }

      one_line = rb_hash_aref(options, ID2SYM(id_one_line));
      if (!NIL_P(one_line)) {
        re2_options.set_one_line(RTEST(one_line));
      }

      p->pattern = new(nothrow) RE2(StringValuePtr(pattern), re2_options);
    } else {
      p->pattern = new(nothrow) RE2(StringValuePtr(pattern));
    }

    if (p->pattern == 0) {
      rb_raise(rb_eNoMemError, "not enough memory to allocate RE2 object");
    }

    return self;
  }

  /*
   * Returns a printable version of the regular expression +re2+.
   *
   * @return [String] a printable version of the regular expression
   * @example
   *   re2 = RE2::Regexp.new("woo?")
   *   re2.inspect    #=> "#<RE2::Regexp /woo?/>"
   */
  static VALUE re2_regexp_inspect(VALUE self) {
    re2_pattern *p;
    VALUE result;
    ostringstream output;

    Data_Get_Struct(self, re2_pattern, p);

    output << "#<RE2::Regexp /" << p->pattern->pattern() << "/>";

    result = ENCODED_STR_NEW(output.str().data(), output.str().length(),
        p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");

    return result;
  }

  /*
   * Returns a string version of the regular expression +re2+.
   *
   * @return [String] a string version of the regular expression
   * @example
   *   re2 = RE2::Regexp.new("woo?")
   *   re2.to_s    #=> "woo?"
   */
  static VALUE re2_regexp_to_s(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return ENCODED_STR_NEW(p->pattern->pattern().data(),
        p->pattern->pattern().size(),
        p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled successfully or not.
   *
   * @return [Boolean] whether or not compilation was successful
   * @example
   *   re2 = RE2::Regexp.new("woo?")
   *   re2.ok?    #=> true
   */
  static VALUE re2_regexp_ok(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->ok());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the utf8 option set to true.
   *
   * @return [Boolean] the utf8 option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :utf8 => true)
   *   re2.utf8?    #=> true
   */
  static VALUE re2_regexp_utf8(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().utf8());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the posix_syntax option set to true.
   *
   * @return [Boolean] the posix_syntax option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :posix_syntax => true)
   *   re2.posix_syntax?    #=> true
   */
  static VALUE re2_regexp_posix_syntax(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().posix_syntax());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the longest_match option set to true.
   *
   * @return [Boolean] the longest_match option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :longest_match => true)
   *   re2.longest_match?    #=> true
   */
  static VALUE re2_regexp_longest_match(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().longest_match());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the log_errors option set to true.
   *
   * @return [Boolean] the log_errors option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :log_errors => true)
   *   re2.log_errors?    #=> true
   */
  static VALUE re2_regexp_log_errors(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().log_errors());
  }

  /*
   * Returns the max_mem setting for the regular expression
   * +re2+.
   *
   * @return [Fixnum] the max_mem option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :max_mem => 1024)
   *   re2.max_mem    #=> 1024
   */
  static VALUE re2_regexp_max_mem(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return INT2FIX(p->pattern->options().max_mem());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the literal option set to true.
   *
   * @return [Boolean] the literal option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :literal => true)
   *   re2.literal?    #=> true
   */
  static VALUE re2_regexp_literal(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().literal());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the never_nl option set to true.
   *
   * @return [Boolean] the never_nl option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :never_nl => true)
   *   re2.never_nl?    #=> true
   */
  static VALUE re2_regexp_never_nl(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().never_nl());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the case_sensitive option set to true.
   *
   * @return [Boolean] the case_sensitive option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :case_sensitive => true)
   *   re2.case_sensitive?    #=> true
   */
  static VALUE re2_regexp_case_sensitive(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().case_sensitive());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the case_sensitive option set to false.
   *
   * @return [Boolean] the inverse of the case_sensitive option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :case_sensitive => true)
   *   re2.case_insensitive?    #=> false
   *   re2.casefold?    #=> false
   */
  static VALUE re2_regexp_case_insensitive(VALUE self) {
    return BOOL2RUBY(re2_regexp_case_sensitive(self) != Qtrue);
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the perl_classes option set to true.
   *
   * @return [Boolean] the perl_classes option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :perl_classes => true)
   *   re2.perl_classes?    #=> true
   */
  static VALUE re2_regexp_perl_classes(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().perl_classes());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the word_boundary option set to true.
   *
   * @return [Boolean] the word_boundary option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :word_boundary => true)
   *   re2.word_boundary?    #=> true
   */
  static VALUE re2_regexp_word_boundary(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().word_boundary());
  }

  /*
   * Returns whether or not the regular expression +re2+
   * was compiled with the one_line option set to true.
   *
   * @return [Boolean] the one_line option
   * @example
   *   re2 = RE2::Regexp.new("woo?", :one_line => true)
   *   re2.one_line?    #=> true
   */
  static VALUE re2_regexp_one_line(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return BOOL2RUBY(p->pattern->options().one_line());
  }

  /*
   * If the RE2 could not be created properly, returns an
   * error string otherwise returns nil.
   *
   * @return [String, nil] the error string or nil
   */
  static VALUE re2_regexp_error(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    if (p->pattern->ok()) {
      return Qnil;
    } else {
      return rb_str_new(p->pattern->error().data(), p->pattern->error().size());
    }
  }

  /*
   * If the RE2 could not be created properly, returns
   * the offending portion of the regexp otherwise returns nil.
   *
   * @return [String, nil] the offending portion of the regexp or nil
   */
  static VALUE re2_regexp_error_arg(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    if (p->pattern->ok()) {
      return Qnil;
    } else {
      return ENCODED_STR_NEW(p->pattern->error_arg().data(),
          p->pattern->error_arg().size(),
          p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1");
    }
  }

  /*
   * Returns the program size, a very approximate measure
   * of a regexp's "cost". Larger numbers are more expensive
   * than smaller numbers.
   *
   * @return [Fixnum] the regexp "cost"
   */
  static VALUE re2_regexp_program_size(VALUE self) {
    re2_pattern *p;
    Data_Get_Struct(self, re2_pattern, p);
    return INT2FIX(p->pattern->ProgramSize());
  }

  /*
   * Returns a hash of the options currently set for
   * +re2+.
   *
   * @return [Hash] the options
   */
  static VALUE re2_regexp_options(VALUE self) {
    VALUE options;
    re2_pattern *p;

    Data_Get_Struct(self, re2_pattern, p);
    options = rb_hash_new();

    rb_hash_aset(options, ID2SYM(id_utf8),
        BOOL2RUBY(p->pattern->options().utf8()));

    rb_hash_aset(options, ID2SYM(id_posix_syntax),
        BOOL2RUBY(p->pattern->options().posix_syntax()));

    rb_hash_aset(options, ID2SYM(id_longest_match),
        BOOL2RUBY(p->pattern->options().longest_match()));

    rb_hash_aset(options, ID2SYM(id_log_errors),
        BOOL2RUBY(p->pattern->options().log_errors()));

    rb_hash_aset(options, ID2SYM(id_max_mem),
        INT2FIX(p->pattern->options().max_mem()));

    rb_hash_aset(options, ID2SYM(id_literal),
        BOOL2RUBY(p->pattern->options().literal()));

    rb_hash_aset(options, ID2SYM(id_never_nl),
        BOOL2RUBY(p->pattern->options().never_nl()));

    rb_hash_aset(options, ID2SYM(id_case_sensitive),
        BOOL2RUBY(p->pattern->options().case_sensitive()));

    rb_hash_aset(options, ID2SYM(id_perl_classes),
        BOOL2RUBY(p->pattern->options().perl_classes()));

    rb_hash_aset(options, ID2SYM(id_word_boundary),
        BOOL2RUBY(p->pattern->options().word_boundary()));

    rb_hash_aset(options, ID2SYM(id_one_line),
        BOOL2RUBY(p->pattern->options().one_line()));

    /* This is a read-only hash after all... */
    rb_obj_freeze(options);

    return options;
  }

  /*
   * Returns the number of capturing subpatterns, or -1 if the regexp
   * wasn't valid on construction. The overall match ($0) does not
   * count: if the regexp is "(a)(b)", returns 2.
   *
   * @return [Fixnum] the number of capturing subpatterns
   */
  static VALUE re2_regexp_number_of_capturing_groups(VALUE self) {
    re2_pattern *p;

    Data_Get_Struct(self, re2_pattern, p);
    return INT2FIX(p->pattern->NumberOfCapturingGroups());
  }

  /*
   * Returns a hash of names to capturing indices of groups.
   *
   * @return [Hash] a hash of names to capturing indices
   */
  static VALUE re2_regexp_named_capturing_groups(VALUE self) {
    VALUE capturing_groups;
    re2_pattern *p;
    map<string, int> groups;
    map<string, int>::iterator iterator;

    Data_Get_Struct(self, re2_pattern, p);
    groups = p->pattern->NamedCapturingGroups();
    capturing_groups = rb_hash_new();

    for (iterator = groups.begin(); iterator != groups.end(); iterator++) {
      rb_hash_aset(capturing_groups,
          ENCODED_STR_NEW(iterator->first.data(), iterator->first.size(),
            p->pattern->options().utf8() ? "UTF-8" : "ISO-8859-1"),
          INT2FIX(iterator->second));
    }

    return capturing_groups;
  }

  /*
   * Match the pattern against the given +text+ and return either
   * a boolean (if no submatches are required) or a {RE2::MatchData}
   * instance.
   *
   * @return [Boolean, RE2::MatchData]
   *
   * @overload match(text)
   *   Returns an {RE2::MatchData} containing the matching
   *   pattern and all subpatterns resulting from looking for
   *   the regexp in +text+.
   *
   *   @param [String] text the text to search
   *   @return [RE2::MatchData] the matches
   *   @raise [NoMemoryError] if there was not enough memory to allocate the matches
   *   @example
   *     r = RE2::Regexp.new('w(o)(o)')
   *     r.match('woo')    #=> #<RE2::MatchData "woo" 1:"o" 2:"o">
   *
   * @overload match(text, 0)
   *   Returns either true or false indicating whether a
   *   successful match was made.
   *
   *   @param [String] text the text to search
   *   @return [Boolean] whether the match was successful
   *   @raise [NoMemoryError] if there was not enough memory to allocate the matches
   *   @example
   *     r = RE2::Regexp.new('w(o)(o)')
   *     r.match('woo', 0) #=> true
   *     r.match('bob', 0) #=> false
   *
   * @overload match(text, number_of_matches)
   *   See +match(text)+ but with a specific number of
   *   matches returned (padded with nils if necessary).
   *
   *   @param [String] text the text to search
   *   @param [Fixnum] number_of_matches the number of matches to return
   *   @return [RE2::MatchData] the matches
   *   @raise [NoMemoryError] if there was not enough memory to allocate the matches
   *   @example
   *     r = RE2::Regexp.new('w(o)(o)')
   *     r.match('woo', 1) #=> #<RE2::MatchData "woo" 1:"o">
   *     r.match('woo', 3) #=> #<RE2::MatchData "woo" 1:"o" 2:"o" 3:nil>
   */
  static VALUE re2_regexp_match(int argc, VALUE *argv, VALUE self) {
    int n;
    bool matched;
    re2_pattern *p;
    re2_matchdata *m;
    VALUE text, number_of_matches, matchdata;

    rb_scan_args(argc, argv, "11", &text, &number_of_matches);

    Data_Get_Struct(self, re2_pattern, p);

    if (RTEST(number_of_matches)) {
      n = NUM2INT(number_of_matches);
    } else {
      n = p->pattern->NumberOfCapturingGroups();
    }

    if (n == 0) {
      matched = match(p->pattern, StringValuePtr(text), 0,
          static_cast<int>(RSTRING_LEN(text)), RE2::UNANCHORED, 0, 0);
      return BOOL2RUBY(matched);
    } else {

      if (n < 0) {
          n = 0;
      }
      /* Because match returns the whole match as well. */
      n += 1;

      matchdata = rb_class_new_instance(0, 0, re2_cMatchData);
      Data_Get_Struct(matchdata, re2_matchdata, m);
      m->matches = new(nothrow) re2::StringPiece[n];
      m->regexp = self;
      m->text = rb_str_dup(text);
      rb_str_freeze(m->text);

      if (m->matches == 0) {
        rb_raise(rb_eNoMemError,
                 "not enough memory to allocate StringPieces for matches");
      }

      m->number_of_matches = n;

      matched = match(p->pattern, StringValuePtr(m->text), 0,
                      static_cast<int>(RSTRING_LEN(m->text)),
                      RE2::UNANCHORED, m->matches, n);

      if (matched) {
        return matchdata;
      } else {
        return Qnil;
      }
    }
  }

  /*
   * Returns true or false to indicate a successful match.
   * Equivalent to +re2.match(text, 0)+.
   *
   * @return [Boolean] whether the match was successful
   */
  static VALUE re2_regexp_match_query(VALUE self, VALUE text) {
    VALUE argv[2];
    argv[0] = text;
    argv[1] = INT2FIX(0);

    return re2_regexp_match(2, argv, self);
  }

  /*
   * Returns a {RE2::Consumer} for scanning the given text incrementally.
   *
   * @example
   *   c = RE2::Regexp.new('(\w+)').consume("Foo bar baz")
   */
  static VALUE re2_regexp_consume(VALUE self, VALUE text) {
    re2_pattern *p;
    re2_consumer *c;
    VALUE consumer;

    Data_Get_Struct(self, re2_pattern, p);
    consumer = rb_class_new_instance(0, 0, re2_cConsumer);
    Data_Get_Struct(consumer, re2_consumer, c);

    c->input = new(nothrow) re2::StringPiece(StringValuePtr(text));
    c->regexp = self;
    c->text = text;
    c->number_of_capturing_groups = p->pattern->NumberOfCapturingGroups();

    return consumer;
  }

  /*
   * Replaces the first occurrence +pattern+ in +str+ with
   * +rewrite+ <i>in place</i>.
   *
   * @param [String] str the string to modify
   * @param [String, RE2::Regexp] pattern a regexp matching text to be replaced
   * @param [String] rewrite the string to replace with
   * @return [String] the resulting string
   * @example
   *   RE2.Replace("hello there", "hello", "howdy") #=> "howdy there"
   *   re2 = RE2.new("hel+o")
   *   RE2.Replace("hello there", re2, "yo")        #=> "yo there"
   *   text = "Good morning"
   *   RE2.Replace(text, "morn", "even")            #=> "Good evening"
   *   text                                         #=> "Good evening"
   */
  static VALUE re2_Replace(VALUE self, VALUE str, VALUE pattern,
      VALUE rewrite) {

    /* Look out for frozen strings. */
    rb_check_frozen(str);

    UNUSED(self);
    VALUE repl;
    re2_pattern *p;

    /* Convert all the inputs to be pumped into RE2::Replace. */
    string str_as_string(StringValuePtr(str));

    /* Do the replacement. */
    if (rb_obj_is_kind_of(pattern, re2_cRegexp)) {
      Data_Get_Struct(pattern, re2_pattern, p);
      RE2::Replace(&str_as_string, *p->pattern, StringValuePtr(rewrite));
    } else {
      RE2::Replace(&str_as_string, StringValuePtr(pattern),
          StringValuePtr(rewrite));
    }

    /* Save the replacement as a VALUE. */
    repl = rb_str_new(str_as_string.data(), str_as_string.size());

    /* Replace the original string with the replacement. */
    if (RSTRING_LEN(str) != RSTRING_LEN(repl)) {
      rb_str_resize(str, RSTRING_LEN(repl));
    }
    memcpy(RSTRING_PTR(str), RSTRING_PTR(repl), RSTRING_LEN(repl));

    return str;
  }

  /*
   * Replaces every occurrence of +pattern+ in +str+ with
   * +rewrite+ <i>in place</i>.
   *
   * @param [String] str the string to modify
   * @param [String, RE2::Regexp] pattern a regexp matching text to be replaced
   * @param [String] rewrite the string to replace with
   * @return [String] the resulting string
   * @example
   *   RE2.GlobalReplace("hello there", "e", "i")   #=> "hillo thiri"
   *   re2 = RE2.new("oo?")
   *   RE2.GlobalReplace("whoops-doops", re2, "e")  #=> "wheps-deps"
   *   text = "Good morning"
   *   RE2.GlobalReplace(text, "o", "ee")           #=> "Geeeed meerning"
   *   text                                          #=> "Geeeed meerning"
   */
  static VALUE re2_GlobalReplace(VALUE self, VALUE str, VALUE pattern,
                                 VALUE rewrite) {

    /* Look out for frozen strings. */
    rb_check_frozen(str);

    UNUSED(self);

    /* Convert all the inputs to be pumped into RE2::GlobalReplace. */
    re2_pattern *p;
    string str_as_string(StringValuePtr(str));
    VALUE repl;

    /* Do the replacement. */
    if (rb_obj_is_kind_of(pattern, re2_cRegexp)) {
      Data_Get_Struct(pattern, re2_pattern, p);
      RE2::GlobalReplace(&str_as_string, *p->pattern, StringValuePtr(rewrite));
    } else {
      RE2::GlobalReplace(&str_as_string, StringValuePtr(pattern),
                         StringValuePtr(rewrite));
    }

    /* Save the replacement as a VALUE. */
    repl = rb_str_new(str_as_string.data(), str_as_string.size());

    /* Replace the original string with the replacement. */
    if (RSTRING_LEN(str) != RSTRING_LEN(repl)) {
      rb_str_resize(str, RSTRING_LEN(repl));
    }
    memcpy(RSTRING_PTR(str), RSTRING_PTR(repl), RSTRING_LEN(repl));

    return str;
  }

  /*
   * Returns a version of str with all potentially meaningful regexp
   * characters escaped. The returned string, used as a regular
   * expression, will exactly match the original string.
   *
   * @param [String] unquoted the unquoted string
   * @return [String] the escaped string
   * @example
   *   RE2::Regexp.escape("1.5-2.0?")    #=> "1\.5\-2\.0\?"
   */
  static VALUE re2_QuoteMeta(VALUE self, VALUE unquoted) {
    UNUSED(self);
    string quoted_string = RE2::QuoteMeta(StringValuePtr(unquoted));
    return rb_str_new(quoted_string.data(), quoted_string.size());
  }

  void Init_re2(void) {
    re2_mRE2 = rb_define_module("RE2");
    re2_cRegexp = rb_define_class_under(re2_mRE2, "Regexp", rb_cObject);
    re2_cMatchData = rb_define_class_under(re2_mRE2, "MatchData", rb_cObject);
    re2_cConsumer = rb_define_class_under(re2_mRE2, "Consumer", rb_cObject);

    rb_define_alloc_func(re2_cRegexp, (VALUE (*)(VALUE))re2_regexp_allocate);
    rb_define_alloc_func(re2_cMatchData,
        (VALUE (*)(VALUE))re2_matchdata_allocate);
    rb_define_alloc_func(re2_cConsumer,
        (VALUE (*)(VALUE))re2_consumer_allocate);

    rb_define_method(re2_cMatchData, "string",
        RUBY_METHOD_FUNC(re2_matchdata_string), 0);
    rb_define_method(re2_cMatchData, "regexp",
        RUBY_METHOD_FUNC(re2_matchdata_regexp), 0);
    rb_define_method(re2_cMatchData, "to_a",
        RUBY_METHOD_FUNC(re2_matchdata_to_a), 0);
    rb_define_method(re2_cMatchData, "size",
        RUBY_METHOD_FUNC(re2_matchdata_size), 0);
    rb_define_method(re2_cMatchData, "length",
        RUBY_METHOD_FUNC(re2_matchdata_size), 0);
    rb_define_method(re2_cMatchData, "[]", RUBY_METHOD_FUNC(re2_matchdata_aref),
        -1); rb_define_method(re2_cMatchData, "to_s",
          RUBY_METHOD_FUNC(re2_matchdata_to_s), 0);
    rb_define_method(re2_cMatchData, "begin",
                     RUBY_METHOD_FUNC(re2_matchdata_begin),
        -1);
    rb_define_method(re2_cMatchData, "end",
                     RUBY_METHOD_FUNC(re2_matchdata_end),
        -1);
    rb_define_method(re2_cMatchData, "inspect",
        RUBY_METHOD_FUNC(re2_matchdata_inspect), 0);

    rb_define_method(re2_cConsumer, "string",
        RUBY_METHOD_FUNC(re2_consumer_string), 0);
    rb_define_method(re2_cConsumer, "regexp",
        RUBY_METHOD_FUNC(re2_consumer_regexp), 0);
    rb_define_method(re2_cConsumer, "consume",
        RUBY_METHOD_FUNC(re2_consumer_consume), 0);
    rb_define_method(re2_cConsumer, "rewind",
        RUBY_METHOD_FUNC(re2_consumer_rewind), 0);

    rb_define_method(re2_cRegexp, "initialize",
        RUBY_METHOD_FUNC(re2_regexp_initialize), -1);
    rb_define_method(re2_cRegexp, "ok?", RUBY_METHOD_FUNC(re2_regexp_ok), 0);
    rb_define_method(re2_cRegexp, "error", RUBY_METHOD_FUNC(re2_regexp_error),
        0);
    rb_define_method(re2_cRegexp, "error_arg",
        RUBY_METHOD_FUNC(re2_regexp_error_arg), 0);
    rb_define_method(re2_cRegexp, "program_size",
        RUBY_METHOD_FUNC(re2_regexp_program_size), 0);
    rb_define_method(re2_cRegexp, "options",
        RUBY_METHOD_FUNC(re2_regexp_options), 0);
    rb_define_method(re2_cRegexp, "number_of_capturing_groups",
        RUBY_METHOD_FUNC(re2_regexp_number_of_capturing_groups), 0);
    rb_define_method(re2_cRegexp, "named_capturing_groups",
        RUBY_METHOD_FUNC(re2_regexp_named_capturing_groups), 0);
    rb_define_method(re2_cRegexp, "match", RUBY_METHOD_FUNC(re2_regexp_match),
        -1);
    rb_define_method(re2_cRegexp, "match?",
        RUBY_METHOD_FUNC(re2_regexp_match_query), 1);
    rb_define_method(re2_cRegexp, "=~",
        RUBY_METHOD_FUNC(re2_regexp_match_query), 1);
    rb_define_method(re2_cRegexp, "===",
        RUBY_METHOD_FUNC(re2_regexp_match_query), 1);
    rb_define_method(re2_cRegexp, "consume",
        RUBY_METHOD_FUNC(re2_regexp_consume), 1);
    rb_define_method(re2_cRegexp, "to_s", RUBY_METHOD_FUNC(re2_regexp_to_s), 0);
    rb_define_method(re2_cRegexp, "to_str", RUBY_METHOD_FUNC(re2_regexp_to_s),
        0);
    rb_define_method(re2_cRegexp, "pattern", RUBY_METHOD_FUNC(re2_regexp_to_s),
        0);
    rb_define_method(re2_cRegexp, "source", RUBY_METHOD_FUNC(re2_regexp_to_s),
        0);
    rb_define_method(re2_cRegexp, "inspect",
        RUBY_METHOD_FUNC(re2_regexp_inspect), 0);
    rb_define_method(re2_cRegexp, "utf8?", RUBY_METHOD_FUNC(re2_regexp_utf8),
        0);
    rb_define_method(re2_cRegexp, "posix_syntax?",
        RUBY_METHOD_FUNC(re2_regexp_posix_syntax), 0);
    rb_define_method(re2_cRegexp, "longest_match?",
        RUBY_METHOD_FUNC(re2_regexp_longest_match), 0);
    rb_define_method(re2_cRegexp, "log_errors?",
        RUBY_METHOD_FUNC(re2_regexp_log_errors), 0);
    rb_define_method(re2_cRegexp, "max_mem",
        RUBY_METHOD_FUNC(re2_regexp_max_mem), 0);
    rb_define_method(re2_cRegexp, "literal?",
        RUBY_METHOD_FUNC(re2_regexp_literal), 0);
    rb_define_method(re2_cRegexp, "never_nl?",
        RUBY_METHOD_FUNC(re2_regexp_never_nl), 0);
    rb_define_method(re2_cRegexp, "case_sensitive?",
        RUBY_METHOD_FUNC(re2_regexp_case_sensitive), 0);
    rb_define_method(re2_cRegexp, "case_insensitive?",
        RUBY_METHOD_FUNC(re2_regexp_case_insensitive), 0);
    rb_define_method(re2_cRegexp, "casefold?",
        RUBY_METHOD_FUNC(re2_regexp_case_insensitive), 0);
    rb_define_method(re2_cRegexp, "perl_classes?",
        RUBY_METHOD_FUNC(re2_regexp_perl_classes), 0);
    rb_define_method(re2_cRegexp, "word_boundary?",
        RUBY_METHOD_FUNC(re2_regexp_word_boundary), 0);
    rb_define_method(re2_cRegexp, "one_line?",
        RUBY_METHOD_FUNC(re2_regexp_one_line), 0);

    rb_define_module_function(re2_mRE2, "Replace",
        RUBY_METHOD_FUNC(re2_Replace), 3);
    rb_define_module_function(re2_mRE2, "GlobalReplace",
        RUBY_METHOD_FUNC(re2_GlobalReplace), 3);
    rb_define_module_function(re2_mRE2, "QuoteMeta",
        RUBY_METHOD_FUNC(re2_QuoteMeta), 1);
    rb_define_singleton_method(re2_cRegexp, "escape",
        RUBY_METHOD_FUNC(re2_QuoteMeta), 1);
    rb_define_singleton_method(re2_cRegexp, "quote",
        RUBY_METHOD_FUNC(re2_QuoteMeta), 1);
    rb_define_singleton_method(re2_cRegexp, "compile",
        RUBY_METHOD_FUNC(rb_class_new_instance), -1);

    rb_define_global_function("RE2", RUBY_METHOD_FUNC(re2_re2), -1);

    /* Create the symbols used in options. */
    id_utf8 = rb_intern("utf8");
    id_posix_syntax = rb_intern("posix_syntax");
    id_longest_match = rb_intern("longest_match");
    id_log_errors = rb_intern("log_errors");
    id_max_mem = rb_intern("max_mem");
    id_literal = rb_intern("literal");
    id_never_nl = rb_intern("never_nl");
    id_case_sensitive = rb_intern("case_sensitive");
    id_perl_classes = rb_intern("perl_classes");
    id_word_boundary = rb_intern("word_boundary");
    id_one_line = rb_intern("one_line");

    #if 0
      /* Fake so YARD generates the file. */
      rb_mKernel = rb_define_module("Kernel");
    #endif
  }
}
