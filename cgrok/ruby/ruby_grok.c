#include <ruby.h>
#include <grok.h>

VALUE cGrok; /* Grok class object */

static VALUE rGrok_initialize(VALUE self) {
  /* empty */
}

static void rGrok_free(void *p) {
  grok_t *grok = (grok_t *)p;
  grok_free(grok);
  free(grok);
}

VALUE rGrok_new(VALUE klass) {
  VALUE rgrok;
  grok_t *grok = ALLOC(grok_t);
  grok_init(grok);
  //grok->logmask = ~0;
  rgrok = Data_Wrap_Struct(klass, 0, rGrok_free, grok);
  rb_obj_call_init(rgrok, 0, 0);
  return rgrok;
}

VALUE rGrok_compile(VALUE self, VALUE pattern) {
  grok_t *grok;
  char *c_pattern;
  long len;
  int ret;
  Data_Get_Struct(self, grok_t, grok);
  c_pattern = rb_str2cstr(pattern, &len);
  ret = grok_compilen(grok, c_pattern, (int)len);
  if (ret) {
    rb_raise(rb_eArgError, "Compile failed: %s", grok->errstr);
  }

  return Qnil;
}

VALUE rGrok_match(VALUE self, VALUE input) {
  grok_t *grok = NULL;
  grok_match_t gm;
  char *c_input = NULL;
  long len = 0;
  int ret = 0;
  VALUE match = Qnil;

  Data_Get_Struct(self, grok_t, grok);
  c_input = rb_str2cstr(input, &len);
  ret = grok_execn(grok, c_input, (int)len, &gm);
  if (ret < 0) {
    rb_raise(rb_eArgError, "Error from grok_execn: %d", ret);
    return Qnil;
  }

  match = rb_hash_new();
  {
    char *name;
    const char *data;
    int namelen, datalen;
    void *handle;
    handle = grok_match_walk_init(&gm);
    while (grok_match_walk_next(&gm, handle, &name, &namelen, 
                                &data, &datalen) == 0) {
      VALUE key, value;
      key = rb_str_new(name, namelen);
      value = rb_str_new(data, datalen);
      rb_hash_aset(match, key, value);
    }
  }

  return match;
}

VALUE rGrok_add_pattern(VALUE self, VALUE name, VALUE pattern) {
  grok_t *grok = NULL;
  char *c_name= NULL, *c_pattern = NULL;
  long namelen = 0, patternlen = 0;

  c_name = rb_str2cstr(name, &namelen);
  c_pattern = rb_str2cstr(pattern, &patternlen);
  Data_Get_Struct(self, grok_t, grok);

  grok_pattern_add(grok, c_name, namelen, c_pattern, patternlen);
  return Qnil;
}

void Init_Grok() {
  cGrok = rb_define_class("Grok", rb_cObject);
  rb_define_singleton_method(cGrok, "new", rGrok_new, 0);
  rb_define_method(cGrok, "initialize", rGrok_initialize, 0);
  rb_define_method(cGrok, "compile", rGrok_compile, 1);
  rb_define_method(cGrok, "match", rGrok_match, 1);
  rb_define_method(cGrok, "add_pattern", rGrok_add_pattern, 2);
}
