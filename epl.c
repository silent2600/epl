
#include <EXTERN.h>
#include <perl.h>
#include <XSUB.h>

#undef stat
#undef printf
#undef sprintf

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdarg.h>
#include <libgen.h>

#if !defined WINNT
#if !defined _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#endif

#include "emacs-module.h"
#include "code.h"

int plugin_is_GPL_compatible;

static PerlInterpreter *perl_main = NULL;

static emacs_env genvo;
static emacs_env *genv = &genvo;
/* emacs developer told me this is not safe, 
   but it make calling elisp from perl much easier.
*/

#define xstrlen(s) ( ((s) == NULL) ? 0 : strlen((s)) )
#define ELstr(x) genv->make_string(genv, (x), xstrlen(x))
#define ELint(x) genv->make_integer(genv, (x))

static emacs_value call_elisp(const char* func, int nargs, ...);
static emacs_value call_elisp_args(const char* func, int nargs, emacs_value args[]);
/* static emacs_value symbol_value(const char *sym); */
/* static char * symbol_value_str(const char *sym); */
static void xdbg(char *, char*);

EXTERN_C void script_xs_init (pTHX);
EXTERN_C void boot_DynaLoader (pTHX_ CV* cv);

static int perl_inited = 0;
static char epl_root[PATH_MAX] = { '\0' };
const  char *app_package = "EplScriptLoader";
const  char *perl_app_code = PerlLoaderCode;


static bool eq_type(emacs_env *env, emacs_value type, const char *type_str) {
  return env->eq(env, type, env->intern(env, type_str));
}

static char* copy_string(emacs_env *env, emacs_value val) {
  ptrdiff_t len = 0;
  char *str = NULL;
  if ( !eq_type(env, env->type_of(env, val), "string") )
    return str;

  env->copy_string_contents(env, val, NULL, &len);
  if ( len ) {
    str = malloc( len );
    if ( str != NULL ) {
      memset(str, '\0', len);
      env->copy_string_contents(env, val, str, &len);
    }
  }
  return str;
}

void dont_gc(void* ptr) EMACS_NOEXCEPT {}

static emacs_value perl_to_elisp(emacs_env* env, SV* v) {

  if ( sv_isobject(v) ) {
    SvREFCNT_inc(v);
    return env->make_user_ptr(env, dont_gc, v);
  }else if ( SvROK(v) ) {
    switch( SvTYPE(v) ) {
    case SVt_IV:
    case SVt_NV:
    case SVt_PV:
      return perl_to_elisp(env, SvRV(v));
    default:
      return env->intern(env, "nil");
    }
  }else {
    if ( !SvOK(v) ) {
      if ( SvTYPE(v) == SVt_PVAV ) {
	ptrdiff_t len;
	AV* av = (AV*)v;
	SV **el;
	len = av_len( av );
	if (len < 0) return env->intern(env, "nil");
	emacs_value *array = malloc( sizeof(emacs_value) * (len + 1) );
	if ( array == NULL ) return env->intern(env, "nil");

	for ( int i = 0; i <= len; i++ ) {
	  el = (SV**)av_fetch( av, i, 0 );
	  array[i] = perl_to_elisp( env, *el );
	}

	emacs_value Fvector = env->intern(env, "vector");
	emacs_value vec = env->funcall(env, Fvector, len + 1, array);
	free(array);
	return vec;
      }else if ( SvTYPE(v) == SVt_PVHV ) {
	SV* sv;
	HV* hv;
	char *key;
	I32 i, count, len;

	hv = (HV*)v;
	count = hv_iterinit( hv );
	if ( count == 0 ) return env->intern(env, "nil");

	emacs_value Fmake_hash_table = env->intern(env, "make-hash-table");
	emacs_value Qtest = env->intern(env, ":test");
	emacs_value Fequal = env->intern(env, "equal");
	emacs_value make_hash_args[] = {Qtest, Fequal};
	emacs_value hash = env->funcall(env, Fmake_hash_table, 2, make_hash_args);
	emacs_value Fputhash = env->intern(env, "puthash");

	for ( i = 0; i < count; i++ ) {
	  sv = hv_iternextsv(hv, &key, &len);
	  emacs_value key_lisp = env->make_string(env, key, len);
	  emacs_value puthash_args[] = { key_lisp, perl_to_elisp(env, sv), hash };
	  env->funcall(env, Fputhash, 3, puthash_args);
	}
	return hash;

      } else {
	return env->intern(env, "nil");
      }
    }else if ( SvIOK(v) ) {
      return env->make_integer(env, SvIV(v));
    }else if ( SvNOK(v) ) {
      return env->make_float(env, SvNV(v));
    }else if ( SvPOK(v) ) {
      STRLEN len = 0;
      char *ptr = SvPV(v, len);
      return env->make_string(env, ptr, len);
    }else {
      return env->intern(env, "nil");
    }
  }
}

static SV* elisp_to_perl(emacs_env *env, emacs_value v) {
  emacs_value type = env->type_of(env, v);
  if (eq_type(env, type, "user-ptr")) {
    //return (SV*)(env->get_user_ptr(env, v));
    SV *sv = (SV*)(env->get_user_ptr(env, v));
    SvREFCNT_inc(sv);
    return sv;
  }else if (eq_type(env, type, "integer")) {
    return newSViv( env->extract_integer(env,v) );

  }else if (eq_type(env, type, "float")) {
    return newSVnv( env->extract_float(env, v) );

  }else if (eq_type(env, type, "string")) {
    char *str = copy_string(env, v);
    SV* pstr = newSVpv( str, 0 );
    free(str);
    return pstr;

  }else if (env->eq(env, v, env->intern(env, "nil") )) {
    return &PL_sv_undef;

  }else if (env->eq(env, v, env->intern(env, "t") )) {
    return newSViv(1);

  }else if (eq_type(env, type, "vector")) {
    AV* a = newAV();
    for (int i = 0; i < (int)(env->vec_size(env, v)); ++i) {
      av_push(a, elisp_to_perl(env, env->vec_get(env, v, i)));
    }
    return newRV_noinc( (SV*)a );

  }else if (eq_type(env, type, "hash-table")) {
    HV* h = newHV();
    emacs_value Fhash_table_keys = env->intern(env, "hash-table-keys");
    emacs_value Fgethash = env->intern(env, "gethash");
    emacs_value keys_args[] = {v};
    emacs_value keys = env->funcall(env, Fhash_table_keys, 1, keys_args);

    emacs_value list_len = call_elisp("length", 1, keys);
    int count = env->extract_integer(env, list_len);

    for (int i = 0; i < count; i++) {
      emacs_value ei = env->make_integer(env, i);
      emacs_value key = call_elisp("nth", 2, ei, keys);
      char* keystr = copy_string(env, key);
      emacs_value args[] = {key, v};
      emacs_value val = env->funcall(env, Fgethash, 2, args);
      (void*)hv_store(h, keystr, xstrlen(keystr), elisp_to_perl(env, val), 0);
      free(keystr);
    }
    return newRV_noinc( (SV*)h );
  }else {
    return &PL_sv_undef;
  }

}

static emacs_value epl_exec(emacs_env *env, ptrdiff_t nargs, emacs_value args[], void *data) {
  unsigned int count = 0;
  emacs_value ret_value;
  SV *ret_s;
  char *function = copy_string(env, args[0]);
  emacs_value Qnil = env->intern(env, "nil");
  genvo = *env;
  if ( !function )
    return Qnil;

  dSP;
  ENTER;
  SAVETMPS;
  PUSHMARK(sp);

  for (int i = 1; i < nargs; i++) {
      XPUSHs( sv_2mortal( elisp_to_perl(env, args[i]) ) );
  }
  PUTBACK;

  count = perl_call_pv(function, G_EVAL|G_SCALAR);

  SPAGAIN;
  if (SvTRUE (ERRSV)) {
    xdbg( "call_pv error: ", (char*)SvPV_nolen(ERRSV) );
    (void) POPs;		/* poping the 'undef' */
    ret_value = Qnil;
  } else {
    if (count == 0) {
      ret_value = Qnil;
    } else {
      ret_s = newSVsv (POPs);
      ret_value = perl_to_elisp(env, ret_s);
      SvREFCNT_dec (ret_s);
    }
  }

  PUTBACK;
  FREETMPS;
  LEAVE;

  free (function);
  return ret_value;
}

static emacs_value epl_load( emacs_env *env, ptrdiff_t nargs, emacs_value args[], void *data ) {

  emacs_value eval;
  int err = 0;
  struct stat buf;
  emacs_value Qnil = env->intern(env, "nil");
  char *filename = copy_string(env, args[0]);

  char *func = "EplScriptLoader::epl_load_eval_file";
  emacs_value function = env->make_string(env, func, xstrlen(func));
  emacs_value pargs[] = { function, args[0] };
  genvo = *env;
  if ( !filename ) {
    err++;
  }

  if ( stat(filename, &buf) != 0) {
    xdbg( filename, " not found" );
    err++;
  }

  eval = epl_exec(env, 2, pargs, NULL );
  if ( env->eq(env, eval, Qnil) ) {
    xdbg( "error while loading ", filename );
    err++;
  }

  free(filename);
  return err ? Qnil : env->intern(env, "t");
}

/* static */
/* XS (XS_defun) { */

/* } */

static
XS (XS_epl_log) {

  int RETVAL = 0;
  dVAR;
  dXSARGS;

  PERL_UNUSED_VAR(cv);
  PERL_UNUSED_VAR(items);
  dXSTARG;

  emacs_value cbuf = call_elisp("current-buffer", 0);
  emacs_value buf = call_elisp("get-buffer-create", 1, ELstr("*epl*"));
  emacs_value Qnil = genv->intern(genv, "nil");
  call_elisp("display-buffer", 1, buf);
  call_elisp("switch-to-buffer", 1, buf);
  call_elisp("toggle-read-only", 1, ELint(-1));
  call_elisp("goto-char", 1, call_elisp("point-min", 0));

  for (int i = 0; i < items; i++) {
    call_elisp("insert", 1, ELstr( SvPV(ST(i), PL_na) ));
  }

  call_elisp("toggle-read-only", 1, ELint(1));
  call_elisp("set-buffer-modified-p", 1, Qnil);
  call_elisp("switch-to-buffer", 1, cbuf);
  
  XSprePUSH;
  PUSHi((IV)RETVAL);
  XSRETURN(1);
}

static
XS (XS_elisp_exec) {
  emacs_value result, *args = NULL, *argsp = NULL;
  SV *result_sv = NULL;
  char *function = NULL;
  dVAR;
  dXSARGS;

  PERL_UNUSED_VAR(cv);
  PERL_UNUSED_VAR(items);

  if ( items == 0 ) {
    Perl_croak(aTHX_ "Usage: EPL::elisp_exec(elisp_function, [args])");
    function = NULL;
  } else {
    function = SvPV(ST(0), PL_na);
    args = malloc( sizeof(emacs_value) * (items - 1));
    memset(args, '\0', sizeof(emacs_value) * (items - 1));
    argsp = args;
    for (int i = 0; i < items - 1; i++) {
      *argsp = perl_to_elisp(genv, ST(i+1));
      argsp++;
    }
  }

  result = call_elisp_args( function, items - 1, args);
  result_sv = elisp_to_perl( genv, result );
  free(args);

  XSprePUSH;
  XPUSHs(result_sv);
  XSRETURN(1);
}

void script_xs_init(pTHX) {
    dXSUB_SYS;
    PERL_UNUSED_CONTEXT;

    newXS("DynaLoader::boot_DynaLoader", boot_DynaLoader, __FILE__);
    newXS( "EPL::log", (XSUBADDR_t)XS_epl_log, __FILE__ );
    newXS( "EPL::elisp_exec", (XSUBADDR_t)XS_elisp_exec, __FILE__ );
}

static int script_init(void) {
  char *perl_args[] = { "", "-e", "0" };
  int argc = 0;
  char **argv = NULL, **env = NULL;
  PERL_SYS_INIT3(&argc, &argv, &env);
  perl_main = perl_alloc();
  if ( !perl_main ) {
    xdbg("perl_alloc failed",  NULL);
    return 0;
  }

  perl_construct( perl_main );
  PL_exit_flags |= PERL_EXIT_DESTRUCT_END;
  perl_parse( perl_main, script_xs_init, 3, perl_args, NULL );
  perl_eval_pv( perl_app_code, TRUE );

  return 1;
}

/*
static void script_end(void) {
  if (perl_main) {
    perl_destruct( perl_main );
    perl_free( perl_main );
    perl_main = NULL;
    PERL_SYS_TERM();
  }
}
*/

static emacs_value call_elisp(const char* func, int nargs, ...) {
  va_list ap;
  int i;
  emacs_value *args = NULL, *argsp = NULL;
  emacs_value result = genv->intern(genv, "nil");
  emacs_value Efunc = genv->intern(genv, func);

  argsp = args = malloc( sizeof(emacs_value) * nargs );
  va_start(ap, nargs);
  for (i = 0; i < nargs; i++) {
    *argsp = va_arg(ap, emacs_value);
    argsp++;
  }
  va_end(ap);

  result = genv->funcall(genv, Efunc, nargs, args);
  free(args);

  return result;
}

static emacs_value call_elisp_args(const char* func, int nargs, emacs_value args[]) {
  emacs_value Efunc = genv->intern(genv, func);
  return genv->funcall(genv, Efunc, nargs, args);
}

/*
static emacs_value symbol_value(const char *sym) {
  emacs_value esym = genv->intern(genv, sym);
  return call_elisp("symbol-value", 1, esym);
}

static char* symbol_value_str(const char *sym) {
  emacs_value ret = symbol_value(sym);
  char *str = copy_string(genv, ret);
  return str;
}
*/

void xdbg(char *s1, char*s2) {
  size_t len = xstrlen(s1) + xstrlen(s2) + 13; /* NULL become "(null)" */
  char *msg = malloc( len );
  memset(msg, '\0', len);
  sprintf(msg, "%s%s", s1, s2);
  emacs_value emsg = genv->make_string(genv, msg, xstrlen(msg));
  emacs_value buf  = genv->make_string(genv, "*Messages*", 10);
  call_elisp("display-message-or-buffer", 1, emsg);
  call_elisp("display-buffer", 1, buf);
  free(msg);
}

static void bind_function(emacs_env *env, const char *name, emacs_value Sfun) {
  emacs_value Qfset = env->intern(env, "fset");
  emacs_value Qsym = env->intern(env, name);
  emacs_value args[] = { Qsym, Sfun };
  env->funcall(env, Qfset, 2, args);
}

static void provide(emacs_env *env, const char *feature) {
  emacs_value Qfeat = env->intern(env, feature);
  emacs_value Qprovide = env->intern (env, "provide");
  emacs_value args[] = { Qfeat };
  env->funcall(env, Qprovide, 1, args);
}

int emacs_module_init(struct emacs_runtime *ert) {

  char *file_name = NULL, *dir_name = NULL;
  emacs_env *env = ert->get_environment(ert);
  genvo = *env;

  if ( !perl_inited )
    perl_inited = script_init();

#if !defined WINNT
  Dl_info di;
  if ( dladdr( epl_load, &di ) ) {
    file_name = strdup( di.dli_fname );
    dir_name = dirname(file_name);
    strncpy(epl_root, dir_name, PATH_MAX);
    free(file_name);
  }
#else
  emacs_value load_path = call_elisp("symbol-value", 1, env->intern(env, "load-path"));
  emacs_value me = env->make_string(env, "epl.dll", 7);
  emacs_value found = call_elisp("locate-file", 2, me, load_path);
  if (env->is_not_nil(env, found)) {
      file_name = copy_string(env, found);
      dir_name = dirname(file_name);
      strncpy(epl_root, dir_name, PATH_MAX);
      free(file_name);
  }
#endif

  if ( perl_inited && strlen(epl_root) ) {
    char *script = "init.pl";
    char script_path[PATH_MAX] = { '\0' };

    if ( (xstrlen(script) + xstrlen(epl_root)) < PATH_MAX - 1) {
      sprintf(script_path, "%s/%s", epl_root, script);
      emacs_value script_name = env->make_string(env, script_path, xstrlen(script_path));
      emacs_value load_args[] = { script_name };
      epl_load(env, 1, load_args, NULL);
    }
  }

#define DEFUN(lsym, csym, amin, amax, doc, data) \
  bind_function (env, lsym, env->make_function(env, amin, amax, csym, doc, data))
  DEFUN("epl-exec", epl_exec, 1, emacs_variadic_function, "call a perl function", NULL);
  DEFUN("epl-load", epl_load, 1, 1, "load a perl file", NULL);
#undef DEFUN
  provide(env, "epl");

  return 0;
}
