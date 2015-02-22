//
// yajl_sparkling.c
// YAJL bindings for Sparkling
//
// Created by Arpad Goretity
// on 22/02/2015
//
// Licensed under the 2-clause BSD License
//

#include <assert.h>

#include <yajl/yajl_parse.h>
#include <yajl/yajl_gen.h>

#include <spn/ctx.h>
#include <spn/str.h>

// the special 'null' value
static const SpnValue null_value = {
	.type = SPN_TYPE_WEAKUSERINFO,
	.v.p = (void *)&null_value
};


// parser state

typedef struct StackNode {
	SpnValue key; // for maps only
	SpnValue value;
	struct StackNode *next;
} StackNode;

typedef struct ParserState {
	SpnValue root;
	StackNode *stack;
	int explicit_null;
} ParserState;

static ParserState state_init()
{
	return (ParserState) { .root = spn_nilval, .stack = NULL, .explicit_null = 0 };
}

static void state_free(ParserState *state)
{
	StackNode *head = state->stack;

	while (head) {
		StackNode *next = head->next;
		spn_value_release(&head->key);
		spn_value_release(&head->value);
		free(head);
		head = next;
	}
}

static void state_push(ParserState *state, SpnValue collection)
{
	assert(spn_isarray(&collection) || spn_ishashmap(&collection));

	StackNode *node = malloc(sizeof *node);
	node->key = spn_nilval;
	node->value = collection;
	node->next = state->stack;
	state->stack = node;
}

static SpnValue state_pop(ParserState *state)
{
	assert(state->stack);

	StackNode *head = state->stack;
	StackNode *next = head->next;
	SpnValue value = head->value;

	assert(spn_isnil(&head->key));

	free(head);
	state->stack = next;

	return value;
}

static void set_value(ParserState *state, SpnValue value)
{
	if (state->stack == NULL) {
		state->root = value;
	} else if (spn_isarray(&state->stack->value)) {
		SpnArray *array = spn_arrayvalue(&state->stack->value);
		spn_array_push(array, &value);
		spn_value_release(&value);
	} else if (spn_ishashmap(&state->stack->value)) {
		SpnHashMap *hashmap = spn_hashmapvalue(&state->stack->value);
		assert(spn_isstring(&state->stack->key));

		spn_hashmap_set(hashmap, &state->stack->key, &value);
		spn_value_release(&state->stack->key);
		spn_value_release(&value);
		state->stack->key = spn_nilval;
	} else {
		assert("cannot add value to non-collection object" == NULL);
	}
}


// Parser callbacks
static int cb_null(void *ctx)
{
	ParserState *state = ctx;
	SpnValue nullrepr = state->explicit_null ? null_value : spn_nilval;
	set_value(ctx, nullrepr);
	return 1;
}

static int cb_boolean(void *ctx, int boolval)
{
	set_value(ctx, spn_makebool(boolval));
	return 1;
}

static int cb_integer(void *ctx, long long intval)
{
	set_value(ctx, spn_makeint(intval));
	return 1;
}

static int cb_double(void *ctx, double doubleval)
{
	set_value(ctx, spn_makefloat(doubleval));
	return 1;
}

static int cb_string(void *ctx, const unsigned char *strval, size_t length)
{
	set_value(ctx, spn_makestring_len((const char *)(strval), length));
	return 1;
}

static int cb_start_map(void *ctx)
{
	state_push(ctx, spn_makehashmap());
	return 1;
}

static int cb_map_key(void *ctx, const unsigned char *key, size_t length)
{
	ParserState *state = ctx;
	assert(state->stack);
	assert(spn_isnil(&state->stack->key));

	state->stack->key = spn_makestring_len((const char *)(key), length);
	return 1;
}

static int cb_end_map(void *ctx)
{
	set_value(ctx, state_pop(ctx));
	return 1;
}

static int cb_start_array(void *ctx)
{
	state_push(ctx, spn_makearray());
	return 1;
}

static int cb_end_array(void *ctx)
{
	set_value(ctx, state_pop(ctx));
	return 1;
}

static const yajl_callbacks parser_callbacks = {
	.yajl_null        = cb_null,
	.yajl_boolean     = cb_boolean,
	.yajl_integer     = cb_integer,
	.yajl_double      = cb_double,
	.yajl_number      = NULL,
	.yajl_string      = cb_string,
	.yajl_start_map   = cb_start_map,
	.yajl_map_key     = cb_map_key,
	.yajl_end_map     = cb_end_map,
	.yajl_start_array = cb_start_array,
	.yajl_end_array   = cb_end_array
};

// Helper for obtaining the parser's error message
static void error_message_to_spn_context(
	yajl_handle hndl,
	SpnContext *ctx,
	const unsigned char *json,
	size_t length
)
{
	unsigned char *errmsg = yajl_get_error(hndl, 1, json, length);
	const void *args[1] = { errmsg };
	spn_ctx_runtime_error(ctx, "error parsing JSON: %s", args);
	yajl_free_error(hndl, errmsg);
}

static void parser_set_bool_option(
	yajl_handle parser,
	yajl_option opt,
	SpnHashMap *config,
	const char *name
)
{
	SpnValue optval = spn_hashmap_get_strkey(config, name);
	if (spn_isbool(&optval)) {
		yajl_config(parser, opt, spn_boolvalue(&optval));
	}
}

static void state_set_bool_option(
	int *opt,
	SpnHashMap *config,
	const char *name
)
{
	SpnValue optval = spn_hashmap_get_strkey(config, name);
	if (spn_isbool(&optval)) {
		*opt = spn_boolvalue(&optval);
	}
}

static void config_parser(yajl_handle parser, ParserState *state, SpnValue config_obj)
{
	assert(spn_ishashmap(&config_obj));
	SpnHashMap *config = spn_hashmapvalue(&config_obj);

	// Allow C-style comments in JSON
	parser_set_bool_option(parser, yajl_allow_comments, config, "comment");

	// parse 'null' to the special yajl.null value instead of nil
	state_set_bool_option(&state->explicit_null, config, "parse_null");
}

static int json_parse(SpnValue *ret, int argc, SpnValue argv[], void *ctx)
{
	if (argc < 1 || argc > 2) {
		spn_ctx_runtime_error(ctx, "expecting 1 or 2 arguments", NULL);
		return -1;
	}

	if (!spn_isstring(&argv[0])) {
		spn_ctx_runtime_error(ctx, "1st argument must be a string", NULL);
		return -2;
	}

	if (argc >= 2 && !spn_ishashmap(&argv[1])) {
		spn_ctx_runtime_error(ctx, "2nd argument must be a config object", NULL);
		return -3;
	}

	SpnString *strobj = spn_stringvalue(&argv[0]);
	const unsigned char *str = (const unsigned char *)(strobj->cstr);
	size_t length = strobj->len;
	int rv = 0;

	ParserState state = state_init();
	yajl_handle yajl_hndl = yajl_alloc(&parser_callbacks, NULL, &state);

	if (argc >= 2) {
		config_parser(yajl_hndl, &state, argv[1]);
	}

	yajl_status status = yajl_parse(yajl_hndl, str, length);

	if (status != yajl_status_ok) {
		// handle parse error
		rv = -4;
	}

	status = yajl_complete_parse(yajl_hndl);
	if (rv == 0 && status != yajl_status_ok) {
		// garbage after input
		rv = -5;
	}

	if (rv == 0) {
		*ret = state.root;
	} else {
		spn_value_release(&state.root);
		error_message_to_spn_context(yajl_hndl, ctx, str, length);
	}

	yajl_free(yajl_hndl);
	state_free(&state);

	return rv;
}

/*
 * JSON Generator (serializer) API
 */

#define RETURN_IF_FAIL(call) \
	do { \
		if ((call) != yajl_gen_status_ok) { \
			const void *args[1] = { #call }; \
			spn_ctx_runtime_error(ctx, "YAJL error: %s", args); \
			return -1; \
		} \
	} while (0)

static int generate_recursive(yajl_gen gen, SpnValue node, SpnContext *ctx)
{
	switch (spn_valtype(&node)) {
	case SPN_TTAG_NIL: /* this shouldn't really happen */
		RETURN_IF_FAIL(yajl_gen_null(gen));
		break;
	case SPN_TTAG_BOOL:
		RETURN_IF_FAIL(yajl_gen_bool(gen, spn_boolvalue(&node)));
		break;
	case SPN_TTAG_NUMBER:
		if (spn_isint(&node)) {
			RETURN_IF_FAIL(yajl_gen_integer(gen, spn_intvalue(&node)));
		} else {
			RETURN_IF_FAIL(yajl_gen_double(gen, spn_floatvalue(&node)));
		}
		break;
	case SPN_TTAG_STRING: {
		SpnString *strobj = spn_stringvalue(&node);
		const unsigned char *str = (const unsigned char *)(strobj->cstr);
		size_t length = strobj->len;
		RETURN_IF_FAIL(yajl_gen_string(gen, str, length));
		break;
	}
	case SPN_TTAG_ARRAY: {
		RETURN_IF_FAIL(yajl_gen_array_open(gen));

		SpnArray *array = spn_arrayvalue(&node);
		size_t n = spn_array_count(array);

		for (size_t i = 0; i < n; i++) {
			SpnValue elem = spn_array_get(array, i);
			int error = generate_recursive(gen, elem, ctx);
			if (error) {
				return -1;
			}
		}

		RETURN_IF_FAIL(yajl_gen_array_close(gen));
		break;
	}
	case SPN_TTAG_HASHMAP: {
		RETURN_IF_FAIL(yajl_gen_map_open(gen));

		SpnHashMap *hm = spn_hashmapvalue(&node);
		SpnValue key, val;
		size_t cursor = 0;

		while ((cursor = spn_hashmap_next(hm, cursor, &key, &val)) != 0) {
			int error;

			error = generate_recursive(gen, key, ctx);
			if (error) {
				return -1;
			}

			error = generate_recursive(gen, val, ctx);
			if (error) {
				return -1;
			}
		}

		RETURN_IF_FAIL(yajl_gen_map_close(gen));
		break;
	}
	case SPN_TTAG_USERINFO:
		if (spn_value_equal(&node, &null_value)) {
			RETURN_IF_FAIL(yajl_gen_null(gen));
		} else {
			spn_ctx_runtime_error(ctx, "found non-serializable value", NULL);
			return -1;
		}
		break;
	default:
		spn_ctx_runtime_error(ctx, "found value of unknown type", NULL);
		return -1;
	}

	return 0;
}

#undef RETURN_IF_FAIL

static void gen_set_bool_option(
	yajl_gen gen,
	yajl_gen_option opt,
	SpnHashMap *config,
	const char *name
)
{
	SpnValue optval = spn_hashmap_get_strkey(config, name);
	if (spn_isbool(&optval)) {
		yajl_gen_config(gen, opt, spn_boolvalue(&optval));
	}
}

static void gen_set_string_option(
	yajl_gen gen,
	yajl_gen_option opt,
	SpnHashMap *config,
	const char *name
)
{
	SpnValue optval = spn_hashmap_get_strkey(config, name);
	if (spn_isstring(&optval)) {
		SpnString *str = spn_stringvalue(&optval);
		yajl_gen_config(gen, opt, str->cstr);
	}
}

static void config_gen(yajl_gen gen, SpnValue config_obj)
{
	assert(spn_ishashmap(&config_obj));
	SpnHashMap *config = spn_hashmapvalue(&config_obj);

	// Generate indented ('beautified') output
	gen_set_bool_option(gen, yajl_gen_beautify, config, "beautify");

	// When beautifying, use this string to indent.
	gen_set_string_option(gen, yajl_gen_indent_string, config, "indent");

	// Escape slash ('/') [for use with HTML]
	gen_set_bool_option(gen, yajl_gen_escape_solidus, config, "escape_slash");
}

static int json_generate(SpnValue *ret, int argc, SpnValue *argv, void *ctx)
{
	if (argc < 1 || argc > 2) {
		spn_ctx_runtime_error(ctx, "expecting 1 or 2 arguments", NULL);
		return -1;
	}

	if (argc >= 2 && !spn_ishashmap(&argv[1])) {
		spn_ctx_runtime_error(ctx, "2nd argument must be a config object", NULL);
		return -2;
	}

	yajl_gen gen = yajl_gen_alloc(NULL);

	if (argc >= 2) {
		config_gen(gen, argv[1]);
	}

	int error = generate_recursive(gen, argv[0], ctx);

	if (error == 0) {
		const unsigned char *str;
		size_t length;
		yajl_gen_status status = yajl_gen_get_buf(gen, &str, &length);

		if (status == yajl_gen_status_ok) {
			*ret = spn_makestring_len((const char *)str, length);
		} else {
			spn_ctx_runtime_error(ctx, "error generating JSON string", NULL);
			error = -3;
		}
	}

	yajl_gen_free(gen);

	return error;
}

// The module initializer
SPN_LIB_OPEN_FUNC(ctx)
{
	SpnValue module = spn_makehashmap();
	SpnHashMap *hm = spn_hashmapvalue(&module);

	const SpnExtFunc F[] = {
		{ "parse",    json_parse    },
		{ "generate", json_generate }
	};

	const SpnExtValue C[] = {
		{ "null", null_value }
	};

	size_t nf = sizeof F / sizeof F[0];
	size_t nc = sizeof C / sizeof C[0];

	for (size_t i = 0; i < nf; i++) {
		SpnValue fn = spn_makenativefunc(F[i].name, F[i].fn);
		spn_hashmap_set_strkey(hm, F[i].name, &fn);
		spn_value_release(&fn);
	}

	for (size_t i = 0; i < nc; i++) {
		spn_hashmap_set_strkey(hm, C[i].name, &C[i].value);
	}

	return module;
}
