/**
Copyright (c) 2025 Florian Evaldsson

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/

#include <gtk/gtk.h>
#include "gedit-snippets.h"
#include "gedit-snippets-python-handling.h"

char *translate_python_block(const char *globals_code, const char *return_code)
{
	// Wrap the return_code into a Python function
	g_autofree char *wrapped_code=NULL;
	size_t wrapped_code_len=asprintf(&wrapped_code, "def __tempfunc(): %s\n", return_code);

	if(wrapped_code_len==0)
	{
		fprintf(stdout,"%s:%d Could not allocate memory\n",__FILE__,__LINE__);
		return NULL;
	}

	PyObject *globals = PyDict_New();
	PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());

	// Run global setup code
	if (PyRun_String(globals_code, Py_file_input, globals, globals) == NULL)
	{
		PyErr_Print();
		Py_DECREF(globals);
		return NULL;
	}

	// Run the wrapped function definition
	if (PyRun_String(wrapped_code, Py_file_input, globals, globals) == NULL)
	{
		PyErr_Print();
		Py_DECREF(globals);
		return NULL;
	}

	// Call the function
	PyObject *func = PyDict_GetItemString(globals, "__tempfunc");
	if (!func || !PyCallable_Check(func))
	{
		fprintf(stderr, "Function not found or not callable.\n");
		Py_DECREF(globals);
		return NULL;
	}

	PyObject *result = PyObject_CallObject(func, NULL);
	if (!result)
	{
		PyErr_Print();
		Py_DECREF(globals);
		return NULL;
	}

	char *output_str = NULL;
	if (PyUnicode_Check(result))
	{
		const char *utf8 = PyUnicode_AsUTF8(result);
		if (utf8)
			output_str = strdup(utf8); // Make a copy we can return
	}

	Py_DECREF(result);
	Py_DECREF(globals);
	return output_str;
}
