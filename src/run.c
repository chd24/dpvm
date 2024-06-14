/* dpvm: bytecode; T16.049-T20.150; $DVS:time$ */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sched.h>
#include "bytecode.h"
#include "error.h"
#include "init.h"
#include "malloc.h"
#include "object.h"
#include "run.h"
#include "task.h"
#include "thread.h"

#define NSTEPS_PER_CODE_LOG 	30
#define STACK_SIZE		0x10000

int64_t dpvm_fsplit(int64_t ieee, int64_t *pexponent) {
	int64_t mantissa = (ieee & 0xfffffffffffffLL) | 0x10000000000000LL;
	int64_t exponent = ieee >> 52 & 0x7ff;

	if (ieee < 0)
		mantissa = -mantissa;

	if (!exponent)
		exponent = INT64_MIN;
	else if (exponent == 0x7ff)
		exponent = INT64_MAX;
	else
		exponent -= 1023 + 52;

	*pexponent = exponent;
	return mantissa;
}

int64_t dpvm_fmerge(int64_t mantissa, int64_t exponent) {
	if (mantissa < 0)
		mantissa = -mantissa | INT64_MIN;

	if (exponent > INT64_MIN && exponent < INT64_MAX)
		exponent += 1023 + 52;

	return (mantissa & 0x800fffffffffffffLL) | (exponent & 0x7ff) << 52;
}

int dpvm_get_last_code(struct dpvm_object *thread) {
	struct dpvm_object *func;
	int64_t *regs, pc;

	if (dpvm_thread_finished(thread)) 
		return DPVM_ERROR_FINISHED;

	regs = thread->ints + thread->nints - DPVM_THREAD_INT_EXT_END;
	func = thread->links[regs[DPVM_THREAD_INT_EXT_NLINKS]];
	pc = regs[DPVM_THREAD_INT_EXT_PC] - 1;
	if (pc < 0 || pc >= func->ncodes)
		return DPVM_ERROR_END_OF_CODE;
	return func->codes[pc];
}

static int dpvm_push_string(struct dpvm_object *parent, struct dpvm_object *obj, const char *str) {
	int i;
	int err = 0;
	for (i = 0; i < strlen(str); ++i) {
		err = dpvm_push_code(parent, obj, str[i]);
		if (err) break;
	}
	return err;
}

static int dpvm_push_std_object(struct dpvm_object *parent, struct dpvm_object *funcs, const char *name, void *addr) {
	struct dpvm_object *translator = (parent ?
		parent->links[DPVM_THREAD_LINK_TASK]->links[DPVM_TASK_LINK_TRANSLATOR] : funcs->dpvm->translator),
		*obj = dpvm_create_object(parent,
		dpvm_type_of_link(translator->type->links[DPVM_TYPE_IN_ARGS], 3), 0, 0, 0, 0);
	int err = 0;
	if (!obj) err = DPVM_ERROR_CREATE_OBJECT;
	if (!err) err = dpvm_push_string(parent, obj, name);
	if (!err) err = dpvm_push_link(parent, funcs, obj);
	if (!err) err = dpvm_push_int(parent, funcs, (size_t)addr);
	if (obj) dpvm_unlink_object(parent, obj);
	return err;
}

static int64_t dpvm_push_function(struct dpvm_object *parent, struct dpvm_object *funcs, struct dpvm_object *func,
		int64_t stack_limits[5], int thread_flags) {
        int64_t err;
	int i;
	if (!(func->flags & DPVM_OBJECT_FLAG_CHECKED)) {
                err = dpvm_check_function(parent, func, thread_flags
			| DPVM_THREAD_FLAG_FORCE_CHECK | DPVM_THREAD_FLAG_NO_PARENT);
		if (err) return err;
	}
	if (func->machcode == func) {
		return -1ll;
	}
	err = dpvm_push_link(parent, funcs, func);
	if (err) return err;
	err = dpvm_push_int(parent, funcs, (size_t)func->machcode->codes);
	if (err) return err;

	for (i = 0; i < 4; ++i) {
		if (func->machcode->ints[i] > stack_limits[i])
			stack_limits[i] = func->machcode->ints[i];
		if (func->machcode->flags & DPVM_OBJECT_FLAG_LARGE_STACK)
			stack_limits[4] = 1;
	}
	
	return 0;
}

int64_t dpvm_check_function(struct dpvm_object *parent, struct dpvm_object *func, int thread_flags) {
	uint64_t volatile *flags = &func->flags;
	struct dpvm_object *thread = 0, *arg = 0, *arg0 = 0, *checker = (parent && func->dpvm->checker ?
		parent->links[DPVM_THREAD_LINK_TASK]->links[DPVM_TASK_LINK_CHECKER] : func->dpvm->checker),
		*translator = (parent && func->dpvm->translator ?
		parent->links[DPVM_THREAD_LINK_TASK]->links[DPVM_TASK_LINK_TRANSLATOR] : func->dpvm->translator);
	int64_t stack_limits[5] = {0, 0, 0, 0, 0};
	int64_t err = 0;
	int i, internal_thread_flags = thread_flags & ~DPVM_THREAD_FLAG_FORCE_CHECK;

	if (!checker || !translator)
		return DPVM_ERROR_SUCCESS;

	if (!(internal_thread_flags & DPVM_THREAD_FLAG_INTERNAL)) {
		internal_thread_flags &= ~DPVM_THREAD_FLAG_MUST_TRANSLATE;
		internal_thread_flags |= DPVM_THREAD_FLAG_INTERNAL;
	}

	while (*flags & DPVM_OBJECT_FLAG_IN_CHECK) {
		if (parent && parent->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)
			return DPVM_ERROR_FINISHED;
		if (parent && parent->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_INTERNAL) {
			if (thread_flags & (DPVM_THREAD_FLAG_FORCE_CHECK | DPVM_THREAD_FLAG_MUST_TRANSLATE)) {
				if (thread_flags & DPVM_THREAD_FLAG_MUST_TRANSLATE)
					internal_thread_flags |= DPVM_THREAD_FLAG_MUST_TRANSLATE;
				break;
			}
			return DPVM_ERROR_SUCCESS;
		}
		sched_yield();
	}

	if (*flags & DPVM_OBJECT_FLAG_CHECKED) {
		if (func->machcode == func && thread_flags & DPVM_THREAD_FLAG_MUST_TRANSLATE)
			return DPVM_ERROR_NOT_TRANSLATED;
		return DPVM_ERROR_SUCCESS;
	}

	if (*flags & DPVM_OBJECT_FLAG_NOT_CHECKED) {
		return DPVM_ERROR_NOT_CHECKED;
	}

	func->flags |= DPVM_OBJECT_FLAG_IN_CHECK;
	if (!dpvm_object_hash(func, -4ull)) {
		err = DPVM_ERROR_NOT_FINISHED; goto end;
	}

	arg = dpvm_create_object(parent, checker->type->links[DPVM_TYPE_IN_ARGS], 1, 0, 0, 0);
	if (!arg) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	err = dpvm_set_link(parent, arg, 0, func);
	if (err) goto end;

	thread = dpvm_thread_create(checker, arg, 0, 0, parent, internal_thread_flags, 0, 0, 0, 0, 0);
	if (!thread) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	dpvm_unlink_object(parent, arg); arg = 0;

	err = dpvm_thread_start(thread, NULL);
	if (err) goto end;

	err = dpvm_run_thread(thread, func->ncodes << NSTEPS_PER_CODE_LOG);

	if (err != DPVM_ERROR_FINISHED)
		goto end;

	err = dpvm_thread_get_result(&thread);
	if (err) goto end;

	arg0 = thread; thread = 0;

	if (!arg0->nints || (arg0->ints[0] & 1) || arg0->nlinks < 2) {
		err = DPVM_ERROR_NOT_CHECKED;
		goto end;
	}

	if (arg0->ints[0] & 2) stack_limits[4] = 1;

	arg = dpvm_create_object(parent, translator->type->links[DPVM_TYPE_IN_ARGS], 4, 0, 0, 0);
	if (!arg) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	err = dpvm_set_link(parent, arg, 0, func);
	if (err) goto end;
	err = dpvm_set_link(parent, arg, 2, arg0->links[1]);
	if (err) goto end;

	err = dpvm_push_std_object(parent, arg->links[1], "any", func->dpvm->any);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "check", &dpvm_check_function);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "code", func);
	if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "fmerge", &dpvm_fmerge);
        if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "free", &dpvm_free_object);
        if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "fsplit", &dpvm_fsplit);
        if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "hash", &dpvm_object_hash);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "io", &dpvm_thread_io);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "match_type", &dpvm_match_type);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "new", &dpvm_create_object);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "reserve0", &dpvm_reserve_links);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "reserve1", &dpvm_reserve_ints);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "reserve2", &dpvm_reserve_floats);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "reserve3", &dpvm_reserve_codes);
	if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "run", &dpvm_thread_do_run);
        if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "thread", &dpvm_thread_create);
	if (err) goto end;
        err = dpvm_push_std_object(parent, arg->links[1], "wait", &dpvm_thread_do_wait);
        if (err) goto end;

	for (i = 0; i < arg0->links[0]->nlinks; ++i) {
		if (arg0->links[0]->links[i] != func) {
			err = dpvm_push_function(parent, arg->links[1], arg0->links[0]->links[i], stack_limits,
				internal_thread_flags);
			if (err) {
				if (err == -1ll) {
					if (thread_flags & DPVM_THREAD_FLAG_MUST_TRANSLATE)
						err = DPVM_ERROR_NOT_TRANSLATED;
					else
						err = 0;
				}
				goto end;
			}
		}
	}

	err = dpvm_push_std_object(parent, arg->links[1], "stack0", (void *)stack_limits[0]);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "stack1", (void *)stack_limits[1]);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "stack2", (void *)stack_limits[2]);
	if (err) goto end;
	err = dpvm_push_std_object(parent, arg->links[1], "stack3", (void *)stack_limits[3]);
	if (err) goto end;

	{
		const char *arch = 
#if defined(__x86_64__) && !defined(_WIN32)
			"amd64";	
#else
			"unknown";
#endif
		err = dpvm_push_string(parent, arg->links[3], arch);
		if (err) goto end;
	}

	thread = dpvm_thread_create(translator, arg, 0, 0, parent, internal_thread_flags, 0, 0, 0, 0, 0);
	if (!thread) { err = DPVM_ERROR_CREATE_OBJECT; goto end; }
	dpvm_unlink_object(parent, arg); arg = 0;

	err = dpvm_thread_start(thread, NULL);
	if (err) goto end;

	err = dpvm_run_thread(thread, func->ncodes << NSTEPS_PER_CODE_LOG);
	if (err != DPVM_ERROR_FINISHED)
		goto end;

	err = dpvm_thread_get_result(&thread);
	if (err) goto end;

	arg = thread; thread = 0;

	if (arg->nints && !arg->ints[0] && arg->nlinks && arg->links[0]->ncodes &&
			arg->links[0]->nints == 4 && func->machcode == func) {
		struct dpvm_object *mc = arg->links[0];
		size_t size = mc->ncodes, size_max = ((size - 1) | (DPVM_PAGE_SIZE - 1)) + 1;
		int64_t diff = (size_max - mc->ncodes_max) * sizeof(uint8_t);
		void *mem;
		if ((err = dpvm_account_memory(mc->dpvm, parent, diff)))
			goto end;
		mem = dpvm_mmap(size_max, 1);
		if (!mem) {
			dpvm_account_memory(mc->dpvm, parent, -diff);
			err = DPVM_ERROR_MAP;
			goto end;
		}
		memcpy(mem, mc->codes, size);
		if (mc->codes) dpvm_free(mc->codes, mc->ncodes_max);
		mc->codes = mem;
		mc->ncodes = size;
		mc->ncodes_max = size_max;
		mc->flags |= DPVM_OBJECT_FLAG_CODE_MAPPED;
		if (stack_limits[4])
			mc->flags |= DPVM_OBJECT_FLAG_LARGE_STACK;
		if (!dpvm_object_hash(mc, -4ull)) {
			err = DPVM_ERROR_NOT_FINISHED; goto end;
		}
		func->machcode = mc;
		arg->links[0] = arg;
	} else if (func->machcode == func) {
		if (thread_flags & DPVM_THREAD_FLAG_MUST_TRANSLATE)
			err = DPVM_ERROR_NOT_TRANSLATED;
	}

end:
	if (arg0) dpvm_unlink_object(parent, arg0);
	if (arg) dpvm_unlink_object(parent, arg);
	if (thread) dpvm_thread_unlink(thread);

	if (!err)
		*flags = DPVM_OBJECT_FLAG_CHECKED;
	else if (DPVM_ERROR_IS_RETRYABLE(err & 0x1f))
		*flags = 0;
	else
		*flags = DPVM_OBJECT_FLAG_NOT_CHECKED;
	
	return err;
}

int64_t dpvm_run_init(struct dpvm *dpvm) {
        return dpvm_check_function(0, dpvm->checker, 0);
}

int64_t dpvm_run_function(struct dpvm_object *thread) {
	struct dpvm_object *func, *typeout, *stack;
	int64_t *regs, pc, nlinks, nints, nfloats, ncodes;
	int i, res, lst;
	int64_t err;

	if (dpvm_thread_finished(thread)) 
		return DPVM_ERROR_FINISHED;

	regs = thread->ints + thread->nints - DPVM_THREAD_INT_EXT_END;
	func = thread->links[regs[DPVM_THREAD_INT_EXT_NLINKS]];
	pc = regs[DPVM_THREAD_INT_EXT_PC];

	if (pc || func->machcode == func)
		return DPVM_ERROR_NOT_TRANSLATED;

	stack = thread->links[DPVM_THREAD_LINK_STACK];

	lst = (func->machcode->flags & DPVM_OBJECT_FLAG_LARGE_STACK);
	res = dpvm_reserve_links(thread, stack, lst ? STACK_SIZE : func->machcode->ints[0]);
	if (res) return res;
	res = dpvm_reserve_ints(thread, stack, lst ? STACK_SIZE : func->machcode->ints[1]);
	if (res) return res;
	res = dpvm_reserve_floats(thread, stack, lst ? STACK_SIZE : func->machcode->ints[2]);
	if (res) return res;
	res = dpvm_reserve_codes(thread, stack, lst ? STACK_SIZE : func->machcode->ints[3]);
	if (res) return res;

	err = (*(int64_t (*)(struct dpvm_object **, int64_t *, double *, uint8_t *,
			struct dpvm_object *))func->machcode->codes)
		(stack->links + stack->nlinks,
		stack->ints + stack->nints,
		stack->floats + stack->nfloats,
		stack->codes + stack->ncodes,
		thread);
	if (err)
		return err;

	typeout = func->type->links[DPVM_TYPE_OUT_ARGS],
	nlinks  = typeout->ints[DPVM_TYPE_N_LINKS_MIN];
	nints   = typeout->ints[DPVM_TYPE_N_INTS_MIN];
	nfloats = typeout->ints[DPVM_TYPE_N_FLOATS_MIN];
	ncodes  = typeout->ints[DPVM_TYPE_N_CODES_MIN];

	stack->nlinks	= regs[DPVM_THREAD_INT_EXT_STACK_NLINKS]  + nlinks;
	stack->nints	= regs[DPVM_THREAD_INT_EXT_STACK_NINTS]   + nints;
	stack->nfloats	= regs[DPVM_THREAD_INT_EXT_STACK_NFLOATS] + nfloats;
	stack->ncodes	= regs[DPVM_THREAD_INT_EXT_STACK_NCODES]  + ncodes;

	if (thread->nints == DPVM_THREAD_INT_END + DPVM_THREAD_INT_EXT_END) {
		regs[DPVM_THREAD_INT_EXT_NLINKS]++;
		dpvm_thread_finish(thread);
	}

	for (i = regs[DPVM_THREAD_INT_EXT_NLINKS]; i < thread->nlinks; ++i)
		dpvm_unlink_object(thread, thread->links[i]);
	thread->nlinks = regs[DPVM_THREAD_INT_EXT_NLINKS];
	thread->nints -= DPVM_THREAD_INT_EXT_END;
		
	if (dpvm_thread_finished(thread))
		return dpvm_thread_prepare_result(thread, typeout);

	return DPVM_ERROR_SUCCESS;
}

int64_t dpvm_run_thread(struct dpvm_object *thread, int64_t nsteps) {
	struct dpvm_object *func;
	int64_t *regs, pc, err;

	if (dpvm_thread_finished(thread))
		return DPVM_ERROR_FINISHED;

	regs = thread->ints + thread->nints - DPVM_THREAD_INT_EXT_END;
	func = thread->links[regs[DPVM_THREAD_INT_EXT_NLINKS]];
	if (!dpvm_object_hash(func, -4ull))
		return DPVM_ERROR_NOT_FINISHED;
	pc = regs[DPVM_THREAD_INT_EXT_PC];

	if (!pc) {
		if (!(func->flags & DPVM_OBJECT_FLAG_CHECKED)) {
			err = dpvm_check_function(thread, func, DPVM_THREAD_FLAG_NO_PARENT);
                        if (err) {
				struct dpvm_hash *hash;
				if (!(err >> 5) && (hash = dpvm_object_hash(func, -3ull)))
					err |= hash->hash[0] << 17 | (0xf00 | DPVM_CODE_ILL) << 5;
				goto end;
                        }
		}
		if (func->machcode != func && !(thread->ints[DPVM_THREAD_INT_FLAGS] & DPVM_THREAD_FLAG_MUST_TRANSLATE)) {
			err = dpvm_run_function(thread);
			goto end;
		}
	}

	err = dpvm_bytecode_run(thread, nsteps);

end:
	if (!err && thread->links[DPVM_THREAD_LINK_TASK]->ints[DPVM_TASK_INT_FLAGS] & DPVM_TASK_FLAG_FINISH)
		err = DPVM_ERROR_FINISHED;
	if (err == DPVM_ERROR_FINISHED && !dpvm_thread_finished(thread)) {
		dpvm_thread_finish(thread);
                thread->ints[DPVM_THREAD_INT_FLAGS] |= DPVM_THREAD_FLAG_ERROR;
		dpvm_thread_prepare_result(thread, thread->dpvm->any);
	}

	return err;
}	

