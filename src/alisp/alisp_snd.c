/*
 *  ALSA lisp implementation - sound related commands
 *  Copyright (c) 2003 by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as
 *   published by the Free Software Foundation; either version 2.1 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

struct acall_table {
	const char *name;
	struct alisp_object * (*func) (struct alisp_instance *instance, struct acall_table * item, struct alisp_object * args);
	void * xfunc;
	const char *prefix;
};

/*
 *  helper functions
 */

static inline const void *get_pointer(struct alisp_object * obj)
{
	if (obj->type == ALISP_OBJ_POINTER)
		return obj->value.ptr;
	return NULL;
}

static const char *get_string(struct alisp_object * obj, const char * deflt)
{
	if (obj == &alsa_lisp_t)
		return "true";
	if (obj->type == ALISP_OBJ_STRING)
		return obj->value.s;
	if (obj->type == ALISP_OBJ_IDENTIFIER)
		return obj->value.id;
	return deflt;
}

struct flags {
	const char *key;
	unsigned int mask;
}; 

static unsigned int get_flags(struct alisp_object * obj, const struct flags * flags, unsigned int deflt)
{
	const char *key;
	int invert;
	unsigned int result;
	const struct flags *ptr;

	if (obj == &alsa_lisp_nil)
		return deflt;
	result = deflt;
	do {
		key = get_string(obj, NULL);
		if (key) {
			invert = key[0] == '!';
			key += invert;
			ptr = flags;
			while (ptr->key) {
				if (!strcmp(ptr->key, key)) {
					if (invert)
						result &= ~ptr->mask;
					else
						result |= ptr->mask;
					break;
				}
				ptr++;
			}
		}
		obj = cdr(obj);
	} while (obj != &alsa_lisp_nil);
	return result;
}

static const void *get_ptr(struct alisp_object * obj, const char *_ptr_id)
{
	const char *ptr_id;
	
	ptr_id = get_string(car(obj), NULL);
	if (ptr_id == NULL)
		return NULL;
	if (strcmp(ptr_id, _ptr_id))
		return NULL;
	return get_pointer(cdr(obj));
}

static struct alisp_object * new_lexpr(struct alisp_instance * instance, int err)
{
	struct alisp_object * lexpr;

	lexpr = new_object(instance, ALISP_OBJ_CONS);
	if (lexpr == NULL)
		return NULL;
	lexpr->value.c.car = new_integer(instance, err);
	if (lexpr->value.c.car == NULL)
		return NULL;
	lexpr->value.c.cdr = new_object(instance, ALISP_OBJ_CONS);
	if (lexpr->value.c.cdr == NULL)
		return NULL;
	return lexpr;
}

static struct alisp_object * add_cons(struct alisp_instance * instance, struct alisp_object *lexpr, int cdr, const char *id, struct alisp_object *obj)
{
	struct alisp_object * p1;

	if (lexpr == NULL || obj == NULL)
		return NULL;
	if (cdr) {
		p1 = lexpr->value.c.cdr = new_object(instance, ALISP_OBJ_CONS);
	} else {
		p1 = lexpr->value.c.car = new_object(instance, ALISP_OBJ_CONS);
	}
	lexpr = p1;
	if (p1 == NULL)
		return NULL;
	p1->value.c.car = new_object(instance, ALISP_OBJ_CONS);
	if ((p1 = p1->value.c.car) == NULL)
		return NULL;
	p1->value.c.car = new_string(instance, id);
	if (p1->value.c.car == NULL)
		return NULL;
	p1->value.c.cdr = obj;
	return lexpr;
}

static inline struct alisp_object * new_result(struct alisp_instance * instance, int err)
{
	return new_integer(instance, err);
}

static struct alisp_object * new_result1(struct alisp_instance * instance, int err, const char *ptr_id, void *ptr)
{
	struct alisp_object * lexpr, * p1;

	if (err < 0)
		ptr = NULL;
	lexpr = new_object(instance, ALISP_OBJ_CONS);
	if (lexpr == NULL)
		return NULL;
	lexpr->value.c.car = new_integer(instance, err);
	if (lexpr->value.c.car == NULL)
		return NULL;
	p1 = add_cons(instance, lexpr, 1, ptr_id, new_pointer(instance, ptr));
	if (p1 == NULL)
		return NULL;
	return lexpr;
}

static struct alisp_object * new_result2(struct alisp_instance * instance, int err, int val)
{
	struct alisp_object * lexpr, * p1;

	if (err < 0)
		val = 0;
	lexpr = new_lexpr(instance, err);
	if (lexpr == NULL)
		return NULL;
	p1 = lexpr->value.c.cdr;
	p1->value.c.car = new_integer(instance, val);
	if (p1->value.c.car == NULL)
		return NULL;
	return lexpr;
}

static struct alisp_object * new_result3(struct alisp_instance * instance, int err, const char *str)
{
	struct alisp_object * lexpr, * p1;

	if (err < 0)
		str = "";
	lexpr = new_lexpr(instance, err);
	if (lexpr == NULL)
		return NULL;
	p1 = lexpr->value.c.cdr;
	p1->value.c.car = new_string(instance, str);
	if (p1->value.c.car == NULL)
		return NULL;
	return lexpr;
}

/*
 *  macros
 */

/*
 *  HCTL functions
 */

typedef int (*snd_xxx_open_t)(void **rctl, const char *name, int mode);
typedef int (*snd_xxx_open1_t)(void **rctl, void *handle);
typedef int (*snd_xxx_close_t)(void **rctl);
typedef int (*snd_int_intp_t)(int *val);
typedef int (*snd_int_str_t)(const char *str);
typedef int (*snd_int_int_strp_t)(int val, char **str);

static struct alisp_object * FA_xxx_open(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	const char *name;
	int err, mode;
	void *handle;
	static struct flags flags[] = {
		{ "nonblock", SND_CTL_NONBLOCK },
		{ "async", SND_CTL_ASYNC },
		{ "readonly", SND_CTL_READONLY },
		{ NULL, 0 }
	};

	name = get_string(eval(instance, car(args)), NULL);
	if (name == NULL)
		return &alsa_lisp_nil;
	mode = get_flags(eval(instance, car(cdr(args))), flags, 0);
	
	err = ((snd_xxx_open_t)item->xfunc)(&handle, name, mode);
	return new_result1(instance, err, item->prefix, handle);
}

static struct alisp_object * FA_xxx_open1(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	int err;
	void *handle;
	const char *prefix1 = "ctl";

	args = eval(instance, args);
	handle = (void *)get_ptr(args, prefix1);
	if (handle == NULL)
		return &alsa_lisp_nil;
	err = ((snd_xxx_open1_t)item->xfunc)(&handle, handle);
	return new_result1(instance, err, item->prefix, handle);
}

static struct alisp_object * FA_xxx_close(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	void *handle;

	args = eval(instance, args);
	handle = (void *)get_ptr(args, item->prefix);
	if (handle == NULL)
		return &alsa_lisp_nil;
	return new_result(instance, ((snd_xxx_close_t)item->xfunc)(handle));
}

static struct alisp_object * FA_int_intp(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	int val, err;

	args = eval(instance, args);
	if (args->type != ALISP_OBJ_INTEGER)
		return &alsa_lisp_nil;
	val = args->value.i;
	err = ((snd_int_intp_t)item->xfunc)(&val);
	return new_result2(instance, err, val);
}

static struct alisp_object * FA_int_str(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	int err;

	args = eval(instance, args);
	if (args->type != ALISP_OBJ_STRING && args->type != ALISP_OBJ_IDENTIFIER)
		return &alsa_lisp_nil;
	err = ((snd_int_str_t)item->xfunc)(args->value.s);
	return new_result(instance, err);
}

static struct alisp_object * FA_int_int_strp(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	int err;
	char *str;

	args = eval(instance, args);
	if (args->type != ALISP_OBJ_INTEGER)
		return &alsa_lisp_nil;
	err = ((snd_int_int_strp_t)item->xfunc)(args->value.i, &str);
	return new_result3(instance, err, str);
}

static struct alisp_object * FA_card_info(struct alisp_instance * instance, struct acall_table * item, struct alisp_object * args)
{
	snd_ctl_t *handle;
	struct alisp_object * lexpr, * p1;
	snd_ctl_card_info_t *info;
	int err;

	args = eval(instance, args);
	handle = (snd_ctl_t *)get_ptr(args, item->prefix);
	if (handle == NULL)
		return &alsa_lisp_nil;
	snd_ctl_card_info_alloca(&info);
	err = snd_ctl_card_info(handle, info);
	lexpr = new_lexpr(instance, err);
	if (err < 0)
		return lexpr;
	p1 = add_cons(instance, lexpr->value.c.cdr, 0, "id", new_string(instance, snd_ctl_card_info_get_id(info)));
	p1 = add_cons(instance, p1, 1, "driver", new_string(instance, snd_ctl_card_info_get_driver(info)));
	p1 = add_cons(instance, p1, 1, "name", new_string(instance, snd_ctl_card_info_get_name(info)));
	p1 = add_cons(instance, p1, 1, "longname", new_string(instance, snd_ctl_card_info_get_longname(info)));
	p1 = add_cons(instance, p1, 1, "mixername", new_string(instance, snd_ctl_card_info_get_mixername(info)));
	p1 = add_cons(instance, p1, 1, "components", new_string(instance, snd_ctl_card_info_get_components(info)));
	if (p1 == NULL)
		return NULL;
	return lexpr;
}

/*
 *  main code
 */

static struct acall_table acall_table[] = {
	{ "card_get_index", &FA_int_str, (void *)snd_card_get_index, NULL },
	{ "card_get_longname", &FA_int_int_strp, (void *)snd_card_get_longname, NULL },
	{ "card_get_name", &FA_int_int_strp, (void *)snd_card_get_name, NULL },
	{ "card_next", &FA_int_intp, (void *)&snd_card_next, NULL },
	{ "ctl_card_info", &FA_card_info, NULL, "ctl" },
	{ "ctl_close", &FA_xxx_close, (void *)&snd_ctl_close, "ctl" },
	{ "ctl_open", &FA_xxx_open, (void *)&snd_ctl_open, "ctl" },
	{ "hctl_close", &FA_xxx_close, (void *)&snd_hctl_close, "hctl" },
	{ "hctl_open", &FA_xxx_open, (void *)&snd_hctl_open, "hctl" },
	{ "hctl_open_ctl", &FA_xxx_open1, (void *)&snd_hctl_open_ctl, "hctl" },
};

static int acall_compar(const void *p1, const void *p2)
{
	return strcmp(((struct acall_table *)p1)->name,
        	      ((struct acall_table *)p2)->name);
}

static struct alisp_object * F_acall(struct alisp_instance *instance, struct alisp_object * args)
{
	struct alisp_object * p1, *p2;
	struct acall_table key, *item;

	p1 = eval(instance, car(args));
	if (p1->type != ALISP_OBJ_IDENTIFIER && p1->type != ALISP_OBJ_STRING)
		return &alsa_lisp_nil;
	p2 = car(cdr(args));
	key.name = p1->value.s;
	if ((item = bsearch(&key, acall_table,
			    sizeof acall_table / sizeof acall_table[0],
			    sizeof acall_table[0], acall_compar)) != NULL)
		return item->func(instance, item, p2);
	lisp_warn(instance, "acall function %s' is undefined", p1->value.s);
	return &alsa_lisp_nil;
}

static struct alisp_object * F_ahandle(struct alisp_instance *instance ATTRIBUTE_UNUSED, struct alisp_object * args)
{
	return car(cdr(eval(instance, car(args))));
}

static struct alisp_object * F_aerror(struct alisp_instance *instance, struct alisp_object * args)
{
	args = car(eval(instance, car(args)));
	if (args == &alsa_lisp_nil)
		return new_integer(instance, SND_ERROR_ALISP_NIL);
	return args;
}

static struct intrinsic snd_intrinsics[] = {
	{ "acall", F_acall },
	{ "aerror", F_aerror },
	{ "ahandle", F_ahandle },
	{ "aresult", F_ahandle },
};