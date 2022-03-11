/* 
 * This file is part of the Hawkbeans JVM developed by
 * the HExSA Lab at Illinois Institute of Technology.
 *
 * Copyright (c) 2019, Kyle C. Hale <khale@cs.iit.edu>
 *
 * All rights reserved.
 *
 * Author: Kyle C. Hale <khale@cs.iit.edu>
 *
 * This is free software.  You are permitted to use,
 * redistribute, and modify it as specified in the 
 * file "LICENSE.txt".
 */
#include <stdlib.h>
#include <string.h>

#include <types.h>
#include <class.h>
#include <stack.h>
#include <mm.h>
#include <thread.h>
#include <exceptions.h>
#include <bc_interp.h>
#include <gc.h>

extern jthread_t * cur_thread;

/* 
 * Maps internal exception identifiers to fully
 * qualified class paths for the exception classes.
 * Note that the ones without fully qualified paths
 * will not be properly raised. 
 *
 * TODO: add the classes for these
 *
 */
static const char * excp_strs[16] __attribute__((used)) =
{
	"java/lang/NullPointerException",
	"java/lang/IndexOutOfBoundsException",
	"java/lang/ArrayIndexOutOfBoundsException",
	"IncompatibleClassChangeError",
	"java/lang/NegativeArraySizeException",
	"java/lang/OutOfMemoryError",
	"java/lang/ClassNotFoundException",
	"java/lang/ArithmeticException",
	"java/lang/NoSuchFieldError",
	"java/lang/NoSuchMethodError",
	"java/lang/RuntimeException",
	"java/io/IOException",
	"FileNotFoundException",
	"java/lang/InterruptedException",
	"java/lang/NumberFormatException",
	"java/lang/StringIndexOutOfBoundsException",
};

int 
hb_excp_str_to_type (char * str)
{
    for (int i = 0; i < sizeof(excp_strs)/sizeof(char*); i++) {
        if (strstr(excp_strs[i], str))
                return i;
    }
    return -1;
}

int get_excp_line(u2 curr_pc){
	u2 line_num = 1;

	for(u2 i = 0; i < cur_thread->cur_frame->minfo->line_info->line_num_tbl.line_tbl_len; i++){
		if(cur_thread->cur_frame->pc > cur_thread->cur_frame->minfo->line_info->line_num_tbl.entries[i].start_pc)
			line_num = cur_thread->cur_frame->minfo->line_info->line_num_tbl.entries[i].line_num;
	}

	return line_num;
}

#define EXCP_MSG_LEN 128

char* get_excp_message(obj_ref_t * eref){
	native_obj_t * obj = (native_obj_t*)eref->heap_ptr;
	char* msg = (char*)malloc(EXCP_MSG_LEN);
	sprintf(msg, "Exception in thread \"%s\" %s\n\tat %s.%s(%s:%d)",
		cur_thread->name,
		obj->class->name,
		cur_thread->class->name, 
		cur_thread->cur_frame->minfo->name, 
		cur_thread->class->src_file, 
		get_excp_line(cur_thread->cur_frame->pc));

	return msg;
}



/*
 * Throws an exception given an internal ID
 * that refers to an exception type. This is to 
 * be used by the runtime (there is no existing
 * exception object, so we have to create a new one
 * and init it).
 *
 * @return: none. exits on failure.
 *
 */
// WRITE ME
void
hb_throw_and_create_excp (u1 type)
{
	obj_ref_t* or = NULL;
	const char * excp_str = excp_strs[type];
	
	java_class_t* ex_cls = hb_get_or_load_class(excp_str);
	
	if(!ex_cls){
		HB_ERR("Could not resolve class ref in %s", __func__);
		exit(EXIT_FAILURE);
	}

	or = gc_obj_alloc(ex_cls);

	hb_invoke_ctor(or);

	hb_throw_exception(or);
}



/* 
 * gets the exception message from the object 
 * ref referring to the exception object.
 *
 * NOTE: caller must free the string
 *
 */
static char *
get_excp_str (obj_ref_t * eref)
{
	char * ret;
	native_obj_t * obj = (native_obj_t*)eref->heap_ptr;
		
	obj_ref_t * str_ref = obj->fields[0].obj;
	native_obj_t * str_obj;
	obj_ref_t * arr_ref;
	native_obj_t * arr_obj;
	int i;
	
	if (!str_ref) {
		return NULL;
	}

	str_obj = (native_obj_t*)str_ref->heap_ptr;
	
	arr_ref = str_obj->fields[0].obj;

	if (!arr_ref) {
		return NULL;
	}

	arr_obj = (native_obj_t*)arr_ref->heap_ptr;

	ret = malloc(arr_obj->flags.array.length+1);

	for (i = 0; i < arr_obj->flags.array.length; i++) {
		ret[i] = arr_obj->fields[i].char_val;
	}

	ret[i] = 0;

	return ret;
}


/*
 * Throws an exception using an
 * object reference to some exception object (which
 * implements Throwable). To be used with athrow.
 * If we're given a bad reference, we throw a 
 * NullPointerException.
 *
 * @return: none. exits on failure.  
 *
 */
void
hb_throw_exception (obj_ref_t * eref)
{
	native_obj_t * obj = (native_obj_t*)eref->heap_ptr;
	method_info_t* mi;
	code_attr_t* ca;

	char* excp_msg = get_excp_message(eref);

	do{
		if(!cur_thread->cur_frame){
			HB_INFO("%s", excp_msg);
			free(ca);
			exit(EXIT_FAILURE);	
		}

		mi = cur_thread->cur_frame->minfo;
		ca = mi->code_attr;

		for(u2 i = 0; i < ca->excp_table_len; i++){
			if(cur_thread->cur_frame->pc > ca->excp_table[i].end_pc
			|| cur_thread->cur_frame->pc < ca->excp_table[i].start_pc){
				continue;
			}

			if(ca->excp_table[i].catch_type == 0){
				cur_thread->cur_frame->pc = ca->excp_table[i].handler_pc;
				return;
			}

			java_class_t* target_cls = hb_resolve_class(ca->excp_table[i].catch_type, mi->owner);

			if(target_cls == obj->class){
				cur_thread->cur_frame->pc = ca->excp_table[i].handler_pc;
				return;
			}
		}
	} while(!hb_pop_frame(cur_thread));

    exit(EXIT_FAILURE);
}
