/* dpvm: bytecode; T15.409-T19.630; $DVS:time$ */

#define _GNU_SOURCE
#include <string.h>
#include <math.h>
#include "cache.h"
#include "bytecode.h"
#include "object.h"
#include "error.h"
#include "run.h"
#include "thread.h"

enum thread_links {
	LINK_STACK		= DPVM_THREAD_LINK_STACK,
	LINK_OUT		= DPVM_THREAD_LINK_RESULT,
	LINK_IN			= DPVM_THREAD_LINK_ARG,
	LINK_FUNC		= DPVM_THREAD_LINK_FUNC,
	LINK_END		= DPVM_THREAD_LINK_END
};

enum thread_ints {
	INT_STACK_NLINKS	= DPVM_THREAD_INT_EXT_STACK_NLINKS,
	INT_STACK_NINTS		= DPVM_THREAD_INT_EXT_STACK_NINTS,
	INT_STACK_NFLOATS	= DPVM_THREAD_INT_EXT_STACK_NFLOATS,
	INT_STACK_NCODES	= DPVM_THREAD_INT_EXT_STACK_NCODES,
	INT_NLINKS		= DPVM_THREAD_INT_EXT_NLINKS,
	INT_PC			= DPVM_THREAD_INT_EXT_PC,
	INT_END			= DPVM_THREAD_INT_EXT_END
};

int dpvm_bytecode_init(struct dpvm *dpvm) {
	return 0;
}

static int64_t error_code(struct dpvm_object *func, int64_t err, int64_t pos, int code) {
    if (!err || err == DPVM_ERROR_FINISHED || err >> 5)
        return err;
    if (pos >= 0 && pos < 0xf00)
        return func->hash.hash[0] << 17 | pos << 5 | err;
    return func->hash.hash[0] << 17 | (code & 0xff | 0xf00) << 5 | err;
}

#define ERR(n) error_code(func, n, regs[INT_PC] - 1, code)

#define INTS_IN_HASH	(DPVM_HASH_SIZE / sizeof(int64_t))

#define is_const(obj) ((int64_t)(obj)->hash_mark < 0)
#define not_const(obj) if (is_const(obj)) \
                                return ERR(DPVM_ERROR_CONST);
#define min_nlinks(n)  if (stack->nlinks  - regs[INT_STACK_NLINKS]  < (n)) \
                                return ERR(DPVM_ERROR_N_LINK_ARGS);
#define min_nints(n)   if (stack->nints   - regs[INT_STACK_NINTS]   < (n)) \
                                return ERR(DPVM_ERROR_N_INT_ARGS);
#define min_nfloats(n) if (stack->nfloats - regs[INT_STACK_NFLOATS] < (n)) \
                                return ERR(DPVM_ERROR_N_FLOAT_ARGS);
#define min_ncodes(n)  if (stack->ncodes  - regs[INT_STACK_NCODES]  < (n)) \
                                return ERR(DPVM_ERROR_N_CODE_ARGS);
#define match_type(obj,nlnks,n) 						\
	if (!dpvm_match_type(stack->links[stack->nlinks - (nlnks) + (n)]->type, \
			dpvm_type_of_link(obj, n)))				\
                return ERR(DPVM_ERROR_TYPE_MISMATCH);

int64_t dpvm_bytecode_run(struct dpvm_object *thread, int64_t nsteps) {
	struct dpvm *dpvm = thread->dpvm;
	struct dpvm_object *func, *newfunc, *stack, *obj;
        int64_t *regs, *sizes, n, nlinks, nints, nfloats, ncodes, i, diff, err = 0;
        int code;

	if (dpvm_thread_finished(thread)) 
		return DPVM_ERROR_FINISHED;

	not_const(thread);
	regs = thread->ints + thread->nints - INT_END;
	func = thread->links[regs[INT_NLINKS]];
	stack = thread->links[LINK_STACK];
	not_const(stack);

	while (nsteps-- && !err) {

	if (regs[INT_PC] >= func->ncodes)
                return error_code(func, DPVM_ERROR_END_OF_CODE, regs[INT_PC], 0);
	code = func->codes[regs[INT_PC]++];
	
	if (code >= DPVM_CODE_ZERO && code <= DPVM_CODE_NUM_MAX) {
		err = dpvm_push_int(thread, stack, code);
	} else switch(code) {

	case DPVM_CODE_CODE:
		err = dpvm_push_link(thread, stack, func);
		break;

	case DPVM_CODE_UNFIX:
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		if (is_const(obj)) {
			if (obj->nrefs > 1)
				return ERR(DPVM_ERROR_CONST);
			obj->hash_mark = 0;
		}
		break;

	case DPVM_CODE_INFO:
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		err |= dpvm_push_int(thread, stack, obj->type->type->ints[0]);
		err |= dpvm_push_int(thread, stack, obj->type->type->ints[1]);
		err |= dpvm_push_int(thread, stack, obj->type->type->ints[2]);
		err |= dpvm_push_int(thread, stack, obj->type->type->ints[3]);
		err |= dpvm_push_int(thread, stack, obj->nlinks);
		err |= dpvm_push_int(thread, stack, obj->nints);
		err |= dpvm_push_int(thread, stack, obj->nfloats);
		err |= dpvm_push_int(thread, stack, obj->ncodes);
		err |= dpvm_set_link(thread, stack, stack->nlinks - 1, obj->type);
		break;

	case DPVM_CODE_NEW:
		min_nlinks(1);
		min_nints(8);
		obj = stack->links[stack->nlinks - 1];
		if (memcmp(stack->ints + stack->nints - 8, obj->type->type->ints, 4 * sizeof(int64_t)))
                        return ERR(DPVM_ERROR_OBJECT_PARAMS);
		obj = dpvm_create_object(thread, obj,
				stack->ints[stack->nints - 4],
				stack->ints[stack->nints - 3],
				stack->ints[stack->nints - 2],
				stack->ints[stack->nints - 1]);
		if (!obj)
                        return ERR(DPVM_ERROR_CREATE_OBJECT);
		dpvm_unlink_object(thread, stack->links[stack->nlinks - 1]);
		stack->links[stack->nlinks - 1] = obj;
		stack->nints -= 8;
		break;

	case DPVM_CODE_HASH:
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		{
			struct dpvm_hash *hash = dpvm_object_hash(obj, -3ull);
			if (!hash)
                                return ERR(DPVM_ERROR_NOT_FINISHED);
			for (i = 0; i < INTS_IN_HASH; ++i)
				err |= dpvm_push_int(thread, stack, hash->hash[i]);
		}
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		break;

	case DPVM_CODE_FIX:
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		if (!dpvm_object_hash(obj, -4ull))
                        return ERR(DPVM_ERROR_NOT_FINISHED);
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		break;

	case DPVM_CODE_RET:
		obj = func->type->links[DPVM_TYPE_OUT_ARGS];
		sizes = obj->ints;
		nlinks = sizes[DPVM_TYPE_N_LINKS_MIN];
		nints = sizes[DPVM_TYPE_N_INTS_MIN];
		nfloats = sizes[DPVM_TYPE_N_FLOATS_MIN];
		ncodes = sizes[DPVM_TYPE_N_CODES_MIN];

		min_nlinks(nlinks);
		min_nints(nints);
		min_nfloats(nfloats);
		min_ncodes(ncodes);

		for (i = 0; i < nlinks; ++i)
			match_type(obj, nlinks, i);

		n = stack->nlinks - regs[INT_STACK_NLINKS];
		diff = n - nlinks;
		if (diff > 0) {
			for (i = 0; i < diff; ++i)
				dpvm_unlink_object(thread, stack->links[i + regs[INT_STACK_NLINKS]]);
			memmove(stack->links + regs[INT_STACK_NLINKS],
				stack->links + regs[INT_STACK_NLINKS] + diff,
				nlinks * sizeof(void *));
			stack->nlinks -= diff;
		}

		n = stack->nints - regs[INT_STACK_NINTS];
		diff = n - nints;
		if (diff > 0) {
			memmove(stack->ints + regs[INT_STACK_NINTS],
				stack->ints + regs[INT_STACK_NINTS] + diff,
				nints * sizeof(int64_t));
			stack->nints -= diff;
		}

		n = stack->nfloats - regs[INT_STACK_NFLOATS];
		diff = n - nfloats;
		if (diff > 0) {
			memmove(stack->floats + regs[INT_STACK_NFLOATS],
				stack->floats + regs[INT_STACK_NFLOATS] + diff,
				nfloats * sizeof(double));
			stack->nfloats -= diff;
		}

		n = stack->ncodes - regs[INT_STACK_NCODES];
		diff = n - ncodes;
		if (diff > 0) {
			memmove(stack->codes + regs[INT_STACK_NCODES],
				stack->codes + regs[INT_STACK_NCODES] + diff,
				ncodes);
			stack->ncodes -= diff;
		}

		if (thread->nints == INT_END + DPVM_THREAD_INT_END) {
			regs[INT_NLINKS]++;
			dpvm_thread_finish(thread);
		}
		for (i = regs[INT_NLINKS]; i < thread->nlinks; ++i)
			dpvm_unlink_object(thread, thread->links[i]);
		thread->nlinks = regs[INT_NLINKS];
		thread->nints -= INT_END;
		
		if (dpvm_thread_finished(thread))
			return dpvm_thread_prepare_result(thread, func->type->links[DPVM_TYPE_OUT_ARGS]);

		regs -= INT_END;
		func = thread->links[regs[INT_NLINKS]];
		break;

        case DPVM_CODE_WAIT:
                min_nlinks(1);
                obj = stack->links[stack->nlinks - 1];
                if (!dpvm_match_type(obj->type, dpvm_thread_type_transaction(dpvm)))				\
                        return ERR(DPVM_ERROR_TYPE_MISMATCH);

                newfunc = obj->links[0];
                obj = newfunc->type->links[DPVM_TYPE_OUT_ARGS];
                sizes = obj->ints;

                nlinks = sizes[DPVM_TYPE_N_LINKS_MIN];
                nints = sizes[DPVM_TYPE_N_INTS_MIN];
                nfloats = sizes[DPVM_TYPE_N_FLOATS_MIN];
                ncodes = sizes[DPVM_TYPE_N_CODES_MIN];

                if (nlinks > 1) dpvm_reserve_links(thread, stack, nlinks - 1);
                dpvm_reserve_ints(thread, stack, nints);
                dpvm_reserve_floats(thread, stack, nfloats);
                dpvm_reserve_codes(thread, stack, ncodes);

                err |= dpvm_thread_do_wait(stack->links + stack->nlinks, stack->ints + stack->nints,
                              stack->floats + stack->nfloats, stack->codes + stack->ncodes, thread);

                stack->nlinks += nlinks - 1;
                stack->nints += nints;
                stack->nfloats += nfloats;
                stack->ncodes += ncodes;
                break;

        case DPVM_CODE_CALL:
		min_nlinks(1);
		newfunc = stack->links[stack->nlinks - 1];
		if (!dpvm_object_hash(newfunc, -4ull))
                        return ERR(DPVM_ERROR_NOT_FINISHED);
		if (!(newfunc->flags & DPVM_OBJECT_FLAG_CHECKED)) {
			int64_t res = dpvm_check_function(thread, newfunc, DPVM_THREAD_FLAG_NO_PARENT);
                        if (res) return ERR(res);
		}

		obj = newfunc->type->links[DPVM_TYPE_IN_ARGS];
		sizes = obj->ints;
		nlinks = sizes[DPVM_TYPE_N_LINKS_MIN];
		nints = sizes[DPVM_TYPE_N_INTS_MIN];
		nfloats = sizes[DPVM_TYPE_N_FLOATS_MIN];
		ncodes = sizes[DPVM_TYPE_N_CODES_MIN];

		min_nlinks(nlinks + 1);
		min_nints(nints);
		min_nfloats(nfloats);
		min_ncodes(ncodes);

		for (i = 0; i < nlinks; ++i)
			match_type(obj, nlinks + 1, i);

		stack->nlinks--;
		err |= dpvm_push_int(thread, thread, stack->nlinks - nlinks);
		err |= dpvm_push_int(thread, thread, stack->nints - nints);
		err |= dpvm_push_int(thread, thread, stack->nfloats - nfloats);
		err |= dpvm_push_int(thread, thread, stack->ncodes - ncodes);
		err |= dpvm_push_int(thread, thread, thread->nlinks);
		err |= dpvm_push_int(thread, thread, 0);
		err |= dpvm_push_link(thread, thread, newfunc);
		dpvm_unlink_object(thread, newfunc);

		if (newfunc->machcode != newfunc) {
			int64_t res = dpvm_run_function(thread);
                        if (res) return ERR(res);
		} else {
			func = newfunc;
		}
		regs = thread->ints + thread->nints - INT_END;
		break;

        case DPVM_CODE_RUN:
                min_nlinks(1);
                newfunc = stack->links[stack->nlinks - 1];
                if (!dpvm_object_hash(newfunc, -4ull))
                        return ERR(DPVM_ERROR_NOT_FINISHED);
                if (!(newfunc->flags & DPVM_OBJECT_FLAG_CHECKED)) {
                        int64_t res = dpvm_check_function(thread, newfunc, DPVM_THREAD_FLAG_NO_PARENT);
                        if (res) return ERR(res);
                }

                obj = newfunc->type->links[DPVM_TYPE_IN_ARGS];
                sizes = obj->ints;
                nlinks = sizes[DPVM_TYPE_N_LINKS_MIN];
                nints = sizes[DPVM_TYPE_N_INTS_MIN];
                nfloats = sizes[DPVM_TYPE_N_FLOATS_MIN];
                ncodes = sizes[DPVM_TYPE_N_CODES_MIN];

                min_nlinks(nlinks + 1);
                min_nints(nints);
                min_nfloats(nfloats);
                min_ncodes(ncodes);

                for (i = 0; i < nlinks; ++i)
                        match_type(obj, nlinks + 1, i);

                err |= dpvm_thread_do_run(stack->links + stack->nlinks, stack->ints + stack->nints,
                              stack->floats + stack->nfloats, stack->codes + stack->ncodes, thread);

                if (!err) {
                        stack->nlinks -= nlinks;
                        stack->nints -= nints;
                        stack->nfloats -= nfloats;
                        stack->ncodes -= ncodes;
                }
                break;

	case DPVM_CODE_LB:
	case DPVM_CODE_LF:
		break;

	case DPVM_CODE_JZB:
	case DPVM_CODE_JNZB:
		min_nints(1);
		if (!stack->ints[--stack->nints] == (code == DPVM_CODE_JNZB))
			break;
	case DPVM_CODE_JB:
		for (i = regs[INT_PC] - 2, n = 1; n || code != DPVM_CODE_LB; i--) {
			if (i < 0)
                                return ERR(DPVM_ERROR_END_OF_CODE);
			switch((code = func->codes[i])) {
			case DPVM_CODE_LF:
			case DPVM_CODE_JB:
			case DPVM_CODE_JZB:
			case DPVM_CODE_JNZB:
				n++;
				break;
			case DPVM_CODE_LB:
			case DPVM_CODE_JF:
			case DPVM_CODE_JZF:
			case DPVM_CODE_JNZF:
				n--;
				break;
			}
		}
		regs[INT_PC] = i + 2;
		break;

	case DPVM_CODE_JZF:
	case DPVM_CODE_JNZF:
		min_nints(1);
		if (!stack->ints[--stack->nints] == (code == DPVM_CODE_JNZF))
			break;
	case DPVM_CODE_JF:
		for (i = regs[INT_PC], n = 1; n || code != DPVM_CODE_LF; i++) {
			if (i >= func->ncodes)
                                return ERR(DPVM_ERROR_END_OF_CODE);
			switch((code = func->codes[i])) {
			case DPVM_CODE_LF:
			case DPVM_CODE_JB:
			case DPVM_CODE_JZB:
			case DPVM_CODE_JNZB:
				n--;
				break;
			case DPVM_CODE_LB:
			case DPVM_CODE_JF:
			case DPVM_CODE_JZF:
			case DPVM_CODE_JNZF:
				n++;
				break;
			}
		}
		regs[INT_PC] = i;
		break;

	case DPVM_CODE_EQ:
		min_nints(2);
		stack->ints[stack->nints - 2] = (stack->ints[stack->nints - 2] == stack->ints[stack->nints - 1]);
		stack->nints--;
		break;

	case DPVM_CODE_LT:
		min_nints(2);
		stack->ints[stack->nints - 2] = (stack->ints[stack->nints - 2] < stack->ints[stack->nints - 1]);
		stack->nints--;
		break;

	case DPVM_CODE_GT:
		min_nints(2);
		stack->ints[stack->nints - 2] = (stack->ints[stack->nints - 2] > stack->ints[stack->nints - 1]);
		stack->nints--;
		break;

	case DPVM_CODE_NEG:
		min_nints(1);
		stack->ints[stack->nints - 1] = -stack->ints[stack->nints - 1];
		break;

	case DPVM_CODE_FEQ:
		min_nfloats(2);
		n = (stack->floats[stack->nfloats - 2] == stack->floats[stack->nfloats - 1]);
		stack->nfloats -= 2;
		err |= dpvm_push_int(thread, stack, n);
		break;

	case DPVM_CODE_FLT:
		min_nfloats(2);
		n = (stack->floats[stack->nfloats - 2] < stack->floats[stack->nfloats - 1]);
		stack->nfloats -= 2;
		err |= dpvm_push_int(thread, stack, n);
		break;

	case DPVM_CODE_FGT:
		min_nfloats(2);
		n = (stack->floats[stack->nfloats - 2] > stack->floats[stack->nfloats - 1]);
		stack->nfloats -= 2;
		err |= dpvm_push_int(thread, stack, n);
		break;

	case DPVM_CODE_FNEG:
		min_nfloats(1);
		stack->floats[stack->nfloats - 1] = -stack->floats[stack->nfloats - 1];
		break;


	case DPVM_CODE_LLOAD:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 1;
		min_nlinks(n);
		stack->nints--;
		err |= dpvm_push_link(thread, stack, stack->links[stack->nlinks - n]);
		break; 

	case DPVM_CODE_ILOAD:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 2;
		min_nints(n);
		stack->ints[stack->nints - 1] = stack->ints[stack->nints - n];
		break; 

	case DPVM_CODE_FLOAD:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 1;
		min_nfloats(n);
		stack->nints--;
		err |= dpvm_push_float(thread, stack, stack->floats[stack->nfloats - n]);
		break;

	case DPVM_CODE_CLOAD:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 1;
		min_ncodes(n);
		stack->nints--;
		err |= dpvm_push_code(thread, stack, stack->codes[stack->ncodes - n]);
		break; 

	case DPVM_CODE_LSTORE:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 2;
		min_nlinks(n);
		stack->nints--;
		dpvm_unlink_object(thread, stack->links[stack->nlinks - n]);
		stack->links[stack->nlinks - n] = stack->links[stack->nlinks - 1];
		stack->nlinks--;
		break; 

	case DPVM_CODE_ISTORE:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 2;
		min_nints(n + 1);
		stack->nints--;
		stack->ints[stack->nints - n] = stack->ints[stack->nints - 1];
		stack->nints--;
		break; 

	case DPVM_CODE_FSTORE:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 2;
		min_nfloats(n);
		stack->nints--;
		stack->floats[stack->nfloats - n] = stack->floats[stack->nfloats - 1];
		stack->nfloats--;
		break;

	case DPVM_CODE_CSTORE:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 2;
		min_ncodes(n);
		stack->nints--;
		stack->codes[stack->ncodes - n] = stack->codes[stack->ncodes - 1];
		stack->ncodes--;
		break; 

	case DPVM_CODE_LPOPS:
		min_nlinks(1);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		break;

	case DPVM_CODE_IPOPS:
		min_nints(1);
		stack->nints--;
		break;

	case DPVM_CODE_FPOPS:
		min_nfloats(1);
		stack->nfloats--;
		break;

	case DPVM_CODE_CPOPS:
		min_ncodes(1);
		stack->ncodes--;
		break;

	case DPVM_CODE_LPOPN:
		min_nints(1);
		n = stack->ints[stack->nints - 1];
		min_nlinks(n);
		while (n--)
			dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints--;
		break; 

	case DPVM_CODE_IPOPN:
		min_nints(1);
		n = stack->ints[stack->nints - 1] + 1;
		min_nints(n);
		stack->nints -= n;
		break; 

	case DPVM_CODE_FPOPN:
		min_nints(1);
		n = stack->ints[stack->nints - 1];
		min_nfloats(n);
		stack->nints--;
		stack->nfloats -= n;
		break; 

	case DPVM_CODE_CPOPN:
		min_nints(1);
		n = stack->ints[stack->nints - 1];
		min_ncodes(n);
		stack->nints--;
		stack->ncodes -= n;
		break; 

	case DPVM_CODE_LGET:
		min_nints(1);
		min_nlinks(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
                if (n < 0 || n >= obj->nlinks) return ERR(DPVM_ERROR_LINKS_INDEX);
		stack->nints--;
		dpvm_set_link(thread, stack, stack->nlinks - 1, obj->links[n]);
		break; 

	case DPVM_CODE_IGET:
		min_nints(1);
		min_nlinks(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
                if (n < 0 || n >= obj->nints) return ERR(DPVM_ERROR_INTS_INDEX);
		stack->ints[stack->nints - 1] = obj->ints[n];
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		break; 

	case DPVM_CODE_FGET:
		min_nints(1);
		min_nlinks(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
                if (n < 0 || n >= obj->nfloats) return ERR(DPVM_ERROR_FLOATS_INDEX);
		err = dpvm_push_float(thread, stack, obj->floats[n]);
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		stack->nints--;
		break; 

	case DPVM_CODE_CGET:
		min_nints(1);
		min_nlinks(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
                if (n < 0 || n >= obj->ncodes) return ERR(DPVM_ERROR_CODES_INDEX);
		err = dpvm_push_code(thread, stack, obj->codes[n]);
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		stack->nints--;
		break; 

	case DPVM_CODE_LSET:
		min_nints(1);
		min_nlinks(2);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
                if (n < 0 || n >= obj->nlinks) return ERR(DPVM_ERROR_LINKS_INDEX);
		err = dpvm_set_link(thread, obj, n, stack->links[stack->nlinks - 2]);
		dpvm_unlink_object(thread, obj);
		dpvm_unlink_object(thread, stack->links[stack->nlinks - 2]);
		stack->nints--;
		stack->nlinks -= 2;
		break; 

	case DPVM_CODE_ISET:
		min_nints(2);
		min_nlinks(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
                if (n < 0 || n >= obj->nints) return ERR(DPVM_ERROR_INTS_INDEX);
		obj->ints[n] = stack->ints[stack->nints - 2];
		dpvm_unlink_object(thread, obj);
		stack->nints -= 2;
		stack->nlinks--;
		break; 

	case DPVM_CODE_FSET:
		min_nints(1);
		min_nlinks(1);
		min_nfloats(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
                if (n < 0 || n >= obj->nfloats) return ERR(DPVM_ERROR_FLOATS_INDEX);
		obj->floats[n] = stack->floats[stack->nfloats - 1];
		dpvm_unlink_object(thread, obj);
		stack->nints--;
		stack->nlinks--;
		stack->nfloats--;
		break; 

	case DPVM_CODE_CSET:
		min_nints(1);
		min_nlinks(1);
		min_ncodes(1);
		n = stack->ints[stack->nints - 1];
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
                if (n < 0 || n >= obj->ncodes) return ERR(DPVM_ERROR_CODES_INDEX);
		obj->codes[n] = stack->codes[stack->ncodes - 1];
		dpvm_unlink_object(thread, obj);
		stack->nints--;
		stack->nlinks--;
		stack->ncodes--;
		break; 

	case DPVM_CODE_LPUSH:
		min_nlinks(2);
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
		err = dpvm_push_link(thread, obj, stack->links[stack->nlinks - 2]);
		dpvm_unlink_object(thread, obj);
		dpvm_unlink_object(thread, stack->links[stack->nlinks - 2]);
		stack->nlinks -= 2;
		break;

	case DPVM_CODE_IPUSH:
		min_nints(1);
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
		err = dpvm_push_int(thread, obj, stack->ints[stack->nints - 1]);
		dpvm_unlink_object(thread, obj);
		stack->nints--;
		stack->nlinks--;
		break;

	case DPVM_CODE_FPUSH:
		min_nfloats(1);
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
		err = dpvm_push_float(thread, obj, stack->floats[stack->nfloats - 1]);
		dpvm_unlink_object(thread, obj);
		stack->nfloats--;
		stack->nlinks--;
		break;

	case DPVM_CODE_CPUSH:
		min_ncodes(1);
		min_nlinks(1);
		obj = stack->links[stack->nlinks - 1];
		not_const(obj);
		err = dpvm_push_code(thread, obj, stack->codes[stack->ncodes - 1]);
		dpvm_unlink_object(thread, obj);
		stack->ncodes--;
		stack->nlinks--;
		break;

	case DPVM_CODE_LPOP:
		min_nlinks(1);
		min_nints(1)
		obj = stack->links[stack->nlinks - 1];
		n = stack->ints[stack->nints - 1];
		not_const(obj);
		if (n < 0 || n > obj->nlinks || obj->nlinks - n < obj->type->ints[DPVM_TYPE_N_LINKS_MIN]) 
                        return ERR(DPVM_ERROR_LINKS_INDEX);
		for (i = 1; i <= n; ++i)
			dpvm_unlink_object(thread, obj->links[obj->nlinks - i]);
		obj->nlinks -= n;
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		stack->nints--;
		break;

	case DPVM_CODE_IPOP:
		min_nlinks(1);
		min_nints(1)
		obj = stack->links[stack->nlinks - 1];
		n = stack->ints[stack->nints - 1];
		not_const(obj);
		if (n < 0 || n > obj->nints || obj->nints - n < obj->type->ints[DPVM_TYPE_N_INTS_MIN]) 
                        return ERR(DPVM_ERROR_INTS_INDEX);
		obj->nints -= n;
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		stack->nints--;
		break;

	case DPVM_CODE_FPOP:
		min_nlinks(1);
		min_nints(1)
		obj = stack->links[stack->nlinks - 1];
		n = stack->ints[stack->nints - 1];
		not_const(obj);
		if (n < 0 || n > obj->nfloats || obj->nfloats - n < obj->type->ints[DPVM_TYPE_N_FLOATS_MIN]) 
                        return ERR(DPVM_ERROR_FLOATS_INDEX);
		obj->nfloats -= n;
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		stack->nints--;
		break;

	case DPVM_CODE_CPOP:
		min_nlinks(1);
		min_nints(1)
		obj = stack->links[stack->nlinks - 1];
		n = stack->ints[stack->nints - 1];
		not_const(obj);
		if (n < 0 || n > obj->ncodes || obj->ncodes - n < obj->type->ints[DPVM_TYPE_N_CODES_MIN]) 
                        return ERR(DPVM_ERROR_CODES_INDEX);
		obj->ncodes -= n;
		dpvm_unlink_object(thread, obj);
		stack->nlinks--;
		stack->nints--;
		break;


	case DPVM_CODE_ADD:
		min_nints(2);
		stack->ints[stack->nints - 2] += stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_SUB:
		min_nints(2);
		stack->ints[stack->nints - 2] -= stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_MUL:
		min_nints(2);
		{
#ifdef __SIZEOF_INT128__
			__uint128_t x = (uint64_t)stack->ints[stack->nints - 2];
			x *= (uint64_t)stack->ints[stack->nints - 1];
			stack->ints[stack->nints - 1] = (uint64_t)(x >> 64);
			stack->ints[stack->nints - 2] = (uint64_t)x;
#else
			uint64_t x = stack->ints[stack->nints - 2], y = stack->ints[stack->nints - 1],
					h = (x >> 32) * (y >> 32), l = (x & 0xffffffffull) * (y * 0xffffffffull), m;
			m = (x >> 32) * (y & 0xffffffffull);
			h += m >> 32;
			m <<= 32;
			if (l + m < l) h++;
			l += m;
			m = (y >> 32) * (x & 0xffffffffull);
			h += m >> 32;
			m <<= 32;
			if (l + m < l) h++;
			l += m;
			stack->ints[stack->nints - 1] = h;
			stack->ints[stack->nints - 2] = l;
#endif
		}
		break;

	case DPVM_CODE_DIV:
		min_nints(2);
		n = stack->ints[stack->nints - 1];
		if (n) {
			stack->ints[stack->nints - 1] = stack->ints[stack->nints - 2] / n;
			stack->ints[stack->nints - 2] %= n;
		}
		break;

	case DPVM_CODE_FADD:
		min_nfloats(2);
		stack->floats[stack->nfloats - 2] += stack->floats[stack->nfloats - 1];
		stack->nfloats--;
		break;

	case DPVM_CODE_FSUB:
		min_nfloats(2);
		stack->floats[stack->nfloats - 2] -= stack->floats[stack->nfloats - 1];
		stack->nfloats--;
		break;

	case DPVM_CODE_FMUL:
		min_nfloats(2);
		stack->floats[stack->nfloats - 2] *= stack->floats[stack->nfloats - 1];
		stack->nfloats--;
		break;

	case DPVM_CODE_FDIV:
		min_nfloats(2);
		stack->floats[stack->nfloats - 2] /= stack->floats[stack->nfloats - 1];
		stack->nfloats--;
		break;

	case DPVM_CODE_SHL:
		min_nints(2);
		stack->ints[stack->nints - 2] <<= stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_SHR:
		min_nints(2);
		((uint64_t *)stack->ints)[stack->nints - 2] >>= stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_AND:
		min_nints(2);
		stack->ints[stack->nints - 2] &= stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_OR:
		min_nints(2);
		stack->ints[stack->nints - 2] |= stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_XOR:
		min_nints(2);
		stack->ints[stack->nints - 2] ^= stack->ints[stack->nints - 1];
		stack->nints--;
		break;

	case DPVM_CODE_NOT:
		min_nints(1);
		stack->ints[stack->nints - 1] = ~stack->ints[stack->nints - 1];
		break;

	case DPVM_CODE_ITOF:
		min_nints(1);
		err = dpvm_push_float(thread, stack, stack->ints[--stack->nints]);
		break;

	case DPVM_CODE_ITOC:
		min_nints(1);
		err = dpvm_push_code(thread, stack, stack->ints[--stack->nints]);
		break;

	case DPVM_CODE_FTOI:
		min_nfloats(1);
		err = dpvm_push_int(thread, stack, stack->floats[--stack->nfloats]);
		break;

	case DPVM_CODE_CTOI:
		min_ncodes(1);
		err = dpvm_push_int(thread, stack, stack->codes[--stack->ncodes]);
		break;

	case DPVM_CODE_FSPLIT:
		min_nfloats(1);
		{
			int64_t mantissa, exponent;
			mantissa = dpvm_fsplit(*(int64_t *)&stack->floats[--stack->nfloats], &exponent);
			err = dpvm_push_int(thread, stack, mantissa);
			err = dpvm_push_int(thread, stack, exponent);
		}
		break;

	case DPVM_CODE_FMERGE:
		min_nints(2);
		{
			int64_t ieee = dpvm_fmerge(stack->ints[stack->nints - 2], stack->ints[stack->nints - 1]);
			stack->nints -= 2;
			err = dpvm_push_float(thread, stack, *(double *)&ieee);
		}
		break;

	case DPVM_CODE_FFLOOR:
		min_nfloats(1);
		stack->floats[stack->nfloats - 1] = floor(stack->floats[stack->nfloats - 1]);
		break;

	case DPVM_CODE_FCEIL:
		min_nfloats(1);
		stack->floats[stack->nfloats - 1] = ceil(stack->floats[stack->nfloats - 1]);
		break;

	case DPVM_CODE_FSQRT:
		min_nfloats(1);
		stack->floats[stack->nfloats - 1] = sqrt(stack->floats[stack->nfloats - 1]);
		break;

	case DPVM_CODE_FEXP:
		min_nfloats(1);
		stack->floats[stack->nfloats - 1] = exp(stack->floats[stack->nfloats - 1]);
		break;

	case DPVM_CODE_FLOG:
		min_nfloats(1);
		stack->floats[stack->nfloats - 1] = log(stack->floats[stack->nfloats - 1]);
		break;

	case DPVM_CODE_FSINCOS:
		min_nfloats(1);
		err = dpvm_reserve_floats(thread, stack, 1);
		if (!err) {
			stack->nfloats++;
#ifdef __APPLE__
			stack->floats[stack->nfloats - 1] = cos(stack->floats[stack->nfloats - 2]);
			stack->floats[stack->nfloats - 2] = sin(stack->floats[stack->nfloats - 2]);
#else
			sincos(stack->floats[stack->nfloats - 2], stack->floats + stack->nfloats - 2,
					stack->floats + stack->nfloats - 1);
#endif
		}
		break;

	case DPVM_CODE_FATAN:
		min_nfloats(2);
		stack->floats[stack->nfloats - 2] = atan2(stack->floats[stack->nfloats - 2], stack->floats[stack->nfloats - 1]);
		stack->nfloats--;
		break;


	case DPVM_CODE_INPUT:
	case DPVM_CODE_INPUTP:
		min_nlinks(2);
		min_nints(2);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 2],
				stack->links[stack->nlinks - 1], 0, 0, thread,
				0, 0, 0, 0, stack->ints[stack->nints - 2], 
				stack->ints[stack->nints - 1]);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints -= 2;
		break;

	case DPVM_CODE_OUTPUT:
	case DPVM_CODE_GETSYS:
	case DPVM_CODE_MLOAD:
	case DPVM_CODE_MSAVE:
	case DPVM_CODE_OUTPUTP:
	case DPVM_CODE_GETSYSP:
	case DPVM_CODE_MLOADP:
	case DPVM_CODE_MSAVEP:
		min_nlinks(3);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 3],
				stack->links[stack->nlinks - 2], 0,
				stack->links[stack->nlinks - 1], thread,
				0, 0, 0, 0, 0, 0);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		break;

	case DPVM_CODE_READ:
	case DPVM_CODE_MSTAT:
	case DPVM_CODE_READP:
	case DPVM_CODE_MSTATP:
		min_nlinks(3);
		min_nints(2);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 3],
				stack->links[stack->nlinks - 2], 
				stack->links[stack->nlinks - 1], 
				0, thread, 0, 0, 0,
				stack->ints[stack->nints - 2],
				stack->ints[stack->nints - 1], 0);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints -= 2;
		break;

	case DPVM_CODE_WRITE:
	case DPVM_CODE_WRITEP:
		min_nlinks(4);
		min_nints(1);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 4],
				stack->links[stack->nlinks - 3], 
				stack->links[stack->nlinks - 2], 
				stack->links[stack->nlinks - 1], 
				thread, 0, 0, 0, 
				stack->ints[stack->nints - 1], 0, 0);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints--;
		break;

	case DPVM_CODE_BIND:
	case DPVM_CODE_BINDP:
		min_nlinks(2);
		min_nints(3);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 2],
				stack->links[stack->nlinks - 1], 0, 0, thread,
				0, 0, 0, stack->ints[stack->nints - 3],
				stack->ints[stack->nints - 2],
				stack->ints[stack->nints - 1]);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints -= 3;
		break;

	case DPVM_CODE_CONNECT:
	case DPVM_CODE_CONNECTP:
		min_nlinks(3);
		min_nints(1);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 3],
				stack->links[stack->nlinks - 2], 
				stack->links[stack->nlinks - 1], 
				0, thread, 0, 0, 0,
				stack->ints[stack->nints - 1], 0, 0);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints--;
		break;

	case DPVM_CODE_SETSYS:
	case DPVM_CODE_SETSYSP:
	case DPVM_CODE_MSEND:
	case DPVM_CODE_MSENDP:
		min_nlinks(4);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 4],
				stack->links[stack->nlinks - 3],
				stack->links[stack->nlinks - 2],
				stack->links[stack->nlinks - 1], thread,
				0, 0, 0, 0, 0, 0);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		break;

	case DPVM_CODE_MRECV:
	case DPVM_CODE_MRECVP:
		min_nlinks(3);
		min_nints(1);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 3],
				stack->links[stack->nlinks - 2], 
				stack->links[stack->nlinks - 1], 
				0, thread, 0, 0, 0,
				0, 0, stack->ints[stack->nints - 1]);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints--;
		break;

	case DPVM_CODE_MPOPEN:
	case DPVM_CODE_MPOPENP:
		min_nlinks(4);
		min_nints(1);
		obj = dpvm_thread_create(stack->links[stack->nlinks - 4],
				stack->links[stack->nlinks - 3],
				stack->links[stack->nlinks - 2], 
				stack->links[stack->nlinks - 1], 
				thread, 0, 0, 0, 0, 0,
				stack->ints[stack->nints - 1]);
		if (obj) err = dpvm_thread_io(obj, code);
		else err = DPVM_ERROR_CREATE_OBJECT;
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		dpvm_unlink_object(thread, stack->links[--stack->nlinks]);
		stack->nints--;
		break;


	default:
                return ERR(DPVM_ERROR_NOT_IMPLEMENTED);
	}

	}

	return ERR(err);
}
