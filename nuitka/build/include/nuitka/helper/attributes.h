//     Copyright 2018, Kay Hayen, mailto:kay.hayen@gmail.com
//
//     Part of "Nuitka", an optimizing Python compiler that is compatible and
//     integrates with CPython, but also works on its own.
//
//     Licensed under the Apache License, Version 2.0 (the "License");
//     you may not use this file except in compliance with the License.
//     You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//     Unless required by applicable law or agreed to in writing, software
//     distributed under the License is distributed on an "AS IS" BASIS,
//     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//     See the License for the specific language governing permissions and
//     limitations under the License.
//
#ifndef __NUITKA_HELPER_ATTRIBUTES_H__
#define __NUITKA_HELPER_ATTRIBUTES_H__

#if PYTHON_VERSION < 300
NUITKA_MAY_BE_UNUSED static PyObject *FIND_ATTRIBUTE_IN_CLASS( PyClassObject *klass, PyObject *attr_name )
{
    CHECK_OBJECT( klass );
    CHECK_OBJECT( attr_name );

    assert( PyClass_Check( klass ));
    assert( PyString_CheckExact( attr_name ) );

    PyObject *result = GET_STRING_DICT_VALUE( (PyDictObject *)klass->cl_dict, (PyStringObject *)attr_name );

    if ( result == NULL )
    {
        Py_ssize_t base_count = PyTuple_Size( klass->cl_bases );

        for ( Py_ssize_t i = 0; i < base_count; i++ )
        {
            result = FIND_ATTRIBUTE_IN_CLASS( (PyClassObject *)PyTuple_GetItem( klass->cl_bases, i ), attr_name );

            if ( result != NULL )
            {
                break;
            }
        }
    }

    return result;
}
#endif

#if PYTHON_VERSION < 300
extern PyObject *CALL_FUNCTION_WITH_ARGS2( PyObject *called, PyObject **args );


static PyObject *LOOKUP_INSTANCE( PyObject *source, PyObject *attr_name )
{
    CHECK_OBJECT( source );
    CHECK_OBJECT( attr_name );

    assert( PyInstance_Check( source ) );
    assert( PyString_CheckExact( attr_name ) );

    PyInstanceObject *source_instance = (PyInstanceObject *)source;

    // The special cases have their own variant on the code generation level
    // as we are called with constants only.
    assert( attr_name != const_str_plain___dict__ );
    assert( attr_name != const_str_plain___class__ );

    // Try the instance dict first.
    PyObject *result = GET_STRING_DICT_VALUE( (PyDictObject *)source_instance->in_dict, (PyStringObject *)attr_name );

    if ( result )
    {
        Py_INCREF( result );
        return result;
    }

    // Next see if a class has it
    result = FIND_ATTRIBUTE_IN_CLASS( source_instance->in_class, attr_name );

    if ( result != NULL )
    {
        descrgetfunc func = Py_TYPE( result )->tp_descr_get;

        if ( func )
        {
            result = func( result, source, (PyObject *)source_instance->in_class );

            if (unlikely( result == NULL ))
            {
                return NULL;
            }

            CHECK_OBJECT( result );

            return result;
        }
        else
        {
            Py_INCREF( result );
            return result;
        }
    }

    // Finally allow a __getattr__ to handle it or else it's an error.
    if (unlikely( source_instance->in_class->cl_getattr == NULL ))
    {
        PyErr_Format(
            PyExc_AttributeError,
            "%s instance has no attribute '%s'",
            PyString_AS_STRING( source_instance->in_class->cl_name ),
            PyString_AS_STRING( attr_name )
        );

        return NULL;
    }
    else
    {
        PyObject *args[] = {
            source,
            attr_name
        };
        return CALL_FUNCTION_WITH_ARGS2(
            source_instance->in_class->cl_getattr,
            args
        );
    }
}
#endif

NUITKA_MAY_BE_UNUSED static PyObject *LOOKUP_ATTRIBUTE( PyObject *source, PyObject *attr_name )
{
    /* Note: There are 2 specializations of this function, that need to be
     * updated in line with this: LOOKUP_ATTRIBUTE_[DICT|CLASS]_SLOT
     */

    CHECK_OBJECT( source );
    CHECK_OBJECT( attr_name );

    PyTypeObject *type = Py_TYPE( source );

    if ( type->tp_getattro == PyObject_GenericGetAttr )
    {
        // Unfortunately this is required, although of cause rarely necessary.
        if (unlikely( type->tp_dict == NULL ))
        {
            if (unlikely( PyType_Ready( type ) < 0 ))
            {
                return NULL;
            }
        }

        PyObject *descr = _PyType_Lookup( type, attr_name );
        descrgetfunc func = NULL;

        if ( descr != NULL )
        {
            Py_INCREF( descr );

#if PYTHON_VERSION < 300
            if ( PyType_HasFeature( Py_TYPE( descr ), Py_TPFLAGS_HAVE_CLASS ) )
            {
#endif
                func = Py_TYPE( descr )->tp_descr_get;

                if ( func != NULL && PyDescr_IsData( descr ) )
                {
                    PyObject *result = func( descr, source, (PyObject *)type );
                    Py_DECREF( descr );

                    return result;
                }
#if PYTHON_VERSION < 300
            }
#endif
        }

        Py_ssize_t dictoffset = type->tp_dictoffset;
        PyObject *dict = NULL;

        if ( dictoffset != 0 )
        {
            // Negative dictionary offsets have special meaning.
            if ( dictoffset < 0 )
            {
                Py_ssize_t tsize;
                size_t size;

                tsize = ((PyVarObject *)source)->ob_size;
                if (tsize < 0)
                    tsize = -tsize;
                size = _PyObject_VAR_SIZE( type, tsize );

                dictoffset += (long)size;
            }

            PyObject **dictptr = (PyObject **) ((char *)source + dictoffset);
            dict = *dictptr;
        }

        if ( dict != NULL )
        {
            CHECK_OBJECT( dict );

            Py_INCREF( dict );

            PyObject *result = PyDict_GetItem( dict, attr_name );

            if ( result != NULL )
            {
                Py_INCREF( result );
                Py_XDECREF( descr );
                Py_DECREF( dict );

                CHECK_OBJECT( result );
                return result;
            }

            Py_DECREF( dict );
        }

        if ( func != NULL )
        {
            PyObject *result = func( descr, source, (PyObject *)type );
            Py_DECREF( descr );

            CHECK_OBJECT( result );
            return result;
        }

        if ( descr != NULL )
        {
            CHECK_OBJECT( descr );
            return descr;
        }

#if PYTHON_VERSION < 300
        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '%s'",
            type->tp_name,
            PyString_AS_STRING( attr_name )
        );
#else
        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '%U'",
            type->tp_name,
            attr_name
        );
#endif
        return NULL;
    }
#if PYTHON_VERSION < 300
    else if ( type->tp_getattro == PyInstance_Type.tp_getattro )
    {
        PyObject *result = LOOKUP_INSTANCE( source, attr_name );
        return result;
    }
#endif
    else if ( type->tp_getattro != NULL )
    {
        PyObject *result = (*type->tp_getattro)( source, attr_name );

        if (unlikely( result == NULL ))
        {
            return NULL;
        }

        CHECK_OBJECT( result );
        return result;
    }
    else if ( type->tp_getattr != NULL )
    {
        PyObject *result = (*type->tp_getattr)( source, (char *)Nuitka_String_AsString_Unchecked( attr_name ) );
        return result;
    }
    else
    {
        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '%s'",
            type->tp_name,
            Nuitka_String_AsString_Unchecked( attr_name )
        );

        return NULL;
    }
}

NUITKA_MAY_BE_UNUSED static PyObject *LOOKUP_ATTRIBUTE_DICT_SLOT( PyObject *source )
{
    CHECK_OBJECT( source );

    PyTypeObject *type = Py_TYPE( source );

    if ( type->tp_getattro == PyObject_GenericGetAttr )
    {
        if (unlikely( type->tp_dict == NULL ))
        {
            if (unlikely( PyType_Ready( type ) < 0 ))
            {
                return NULL;
            }
        }

        PyObject *descr = _PyType_Lookup( type, const_str_plain___dict__ );
        descrgetfunc func = NULL;

        if ( descr != NULL )
        {
            Py_INCREF( descr );

#if PYTHON_VERSION < 300
            if ( PyType_HasFeature( Py_TYPE( descr ), Py_TPFLAGS_HAVE_CLASS ) )
            {
#endif
                func = Py_TYPE( descr )->tp_descr_get;

                if ( func != NULL && PyDescr_IsData( descr ) )
                {
                    PyObject *result = func( descr, source, (PyObject *)type );
                    Py_DECREF( descr );

                    return result;
                }
#if PYTHON_VERSION < 300
            }
#endif
        }

        Py_ssize_t dictoffset = type->tp_dictoffset;
        PyObject *dict = NULL;

        if ( dictoffset != 0 )
        {
            // Negative dictionary offsets have special meaning.
            if ( dictoffset < 0 )
            {
                Py_ssize_t tsize;
                size_t size;

                tsize = ((PyVarObject *)source)->ob_size;
                if (tsize < 0)
                    tsize = -tsize;
                size = _PyObject_VAR_SIZE( type, tsize );

                dictoffset += (long)size;
            }

            PyObject **dictptr = (PyObject **) ((char *)source + dictoffset);
            dict = *dictptr;
        }

        if ( dict != NULL )
        {
            CHECK_OBJECT( dict );

            Py_INCREF( dict );

            PyObject *result = PyDict_GetItem( dict, const_str_plain___dict__ );

            if ( result != NULL )
            {
                Py_INCREF( result );
                Py_XDECREF( descr );
                Py_DECREF( dict );

                CHECK_OBJECT( result );
                return result;
            }

            Py_DECREF( dict );
        }

        if ( func != NULL )
        {
            PyObject *result = func( descr, source, (PyObject *)type );
            Py_DECREF( descr );

            CHECK_OBJECT( result );
            return result;
        }

        if ( descr != NULL )
        {
            CHECK_OBJECT( descr );
            return descr;
        }

        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '__dict__'",
            type->tp_name
        );
        return NULL;
    }
#if PYTHON_VERSION < 300
    else if ( type->tp_getattro == PyInstance_Type.tp_getattro )
    {
        PyInstanceObject *source_instance = (PyInstanceObject *)source;
        PyObject *result = source_instance->in_dict;

        Py_INCREF( result );
        return result;
    }
#endif
    else if ( type->tp_getattro != NULL )
    {
        PyObject *result = (*type->tp_getattro)( source, const_str_plain___dict__ );

        if (unlikely( result == NULL ))
        {
            return NULL;
        }

        CHECK_OBJECT( result );
        return result;
    }
    else if ( type->tp_getattr != NULL )
    {
        PyObject *result = (*type->tp_getattr)( source, (char *)"__dict__" );
        return result;
    }
    else
    {
        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '__dict__'",
            type->tp_name
        );

        return NULL;
    }
}

NUITKA_MAY_BE_UNUSED static PyObject *LOOKUP_ATTRIBUTE_CLASS_SLOT( PyObject *source )
{
    CHECK_OBJECT( source );

    PyTypeObject *type = Py_TYPE( source );

    if ( type->tp_getattro == PyObject_GenericGetAttr )
    {
        if (unlikely( type->tp_dict == NULL ))
        {
            if (unlikely( PyType_Ready( type ) < 0 ))
            {
                return NULL;
            }
        }

        PyObject *descr = _PyType_Lookup( type, const_str_plain___class__ );
        descrgetfunc func = NULL;

        if ( descr != NULL )
        {
            Py_INCREF( descr );

#if PYTHON_VERSION < 300
            if ( PyType_HasFeature( Py_TYPE( descr ), Py_TPFLAGS_HAVE_CLASS ) )
            {
#endif
                func = Py_TYPE( descr )->tp_descr_get;

                if ( func != NULL && PyDescr_IsData( descr ) )
                {
                    PyObject *result = func( descr, source, (PyObject *)type );
                    Py_DECREF( descr );

                    return result;
                }
#if PYTHON_VERSION < 300
            }
#endif
        }

        Py_ssize_t dictoffset = type->tp_dictoffset;
        PyObject *dict = NULL;

        if ( dictoffset != 0 )
        {
            // Negative dictionary offsets have special meaning.
            if ( dictoffset < 0 )
            {
                Py_ssize_t tsize;
                size_t size;

                tsize = ((PyVarObject *)source)->ob_size;
                if (tsize < 0)
                    tsize = -tsize;
                size = _PyObject_VAR_SIZE( type, tsize );

                dictoffset += (long)size;
            }

            PyObject **dictptr = (PyObject **) ((char *)source + dictoffset);
            dict = *dictptr;
        }

        if ( dict != NULL )
        {
            CHECK_OBJECT( dict );

            Py_INCREF( dict );

            PyObject *result = PyDict_GetItem( dict, const_str_plain___class__ );

            if ( result != NULL )
            {
                Py_INCREF( result );
                Py_XDECREF( descr );
                Py_DECREF( dict );

                CHECK_OBJECT( result );
                return result;
            }

            Py_DECREF( dict );
        }

        if ( func != NULL )
        {
            PyObject *result = func( descr, source, (PyObject *)type );
            Py_DECREF( descr );

            CHECK_OBJECT( result );
            return result;
        }

        if ( descr != NULL )
        {
            CHECK_OBJECT( descr );
            return descr;
        }

        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '__class__'",
            type->tp_name
        );
        return NULL;
    }
#if PYTHON_VERSION < 300
    else if ( type->tp_getattro == PyInstance_Type.tp_getattro )
    {
        PyInstanceObject *source_instance = (PyInstanceObject *)source;
        PyObject *result = (PyObject *)source_instance->in_class;

        Py_INCREF( result );
        return result;
    }
#endif
    else if ( type->tp_getattro != NULL )
    {
        PyObject *result = (*type->tp_getattro)( source, const_str_plain___class__ );

        if (unlikely( result == NULL ))
        {
            return NULL;
        }

        CHECK_OBJECT( result );
        return result;
    }
    else if ( type->tp_getattr != NULL )
    {
        PyObject *result = (*type->tp_getattr)( source, (char *)"__class__" );
        return result;
    }
    else
    {
        PyErr_Format(
            PyExc_AttributeError,
            "'%s' object has no attribute '__class__'",
            type->tp_name
        );

        return NULL;
    }
}

NUITKA_MAY_BE_UNUSED static PyObject *BUILTIN_HASATTR( PyObject *source, PyObject *attr_name )
{
    CHECK_OBJECT( source );
    CHECK_OBJECT( attr_name );

#if PYTHON_VERSION < 300

    if ( PyUnicode_Check( attr_name ) )
    {
        attr_name = _PyUnicode_AsDefaultEncodedString( attr_name, NULL );

        if (unlikely( attr_name == NULL ))
        {
            return NULL;
        }
    }

    if (unlikely( !PyString_Check( attr_name ) ))
    {
        PyErr_Format(
            PyExc_TypeError,
            "hasattr(): attribute name must be string"
        );

        return NULL;
    }
#else
    if (unlikely( !PyUnicode_Check( attr_name ) ))
    {
        PyErr_Format(
            PyExc_TypeError,
            "hasattr(): attribute name must be string"
        );

        return NULL;
    }
#endif

    PyObject *value = PyObject_GetAttr( source, attr_name );

    if ( value == NULL )
    {
        if ( PyErr_ExceptionMatches( PyExc_AttributeError ) )
        {
            CLEAR_ERROR_OCCURRED();
            return Py_False;
        }

        return NULL;
    }

    return Py_True;
}

#if PYTHON_VERSION < 300
extern PyObject *CALL_FUNCTION_WITH_ARGS3( PyObject *called, PyObject **args );

static bool SET_INSTANCE( PyObject *target, PyObject *attr_name, PyObject *value )
{
    CHECK_OBJECT( target );
    CHECK_OBJECT( attr_name );
    CHECK_OBJECT( value );

    assert( PyInstance_Check( target ) );
    assert( PyString_Check( attr_name ) );

    PyInstanceObject *target_instance = (PyInstanceObject *)target;

    // The special cases should get their own SET_ATTRIBUTE_xxxx_SLOT variants
    // on the code generation level as SET_ATTRIBUTE is called with constants
    // only.
    assert( attr_name != const_str_plain___dict__ );
    assert( attr_name != const_str_plain___class__ );

    if ( target_instance->in_class->cl_setattr != NULL )
    {
        PyObject *args[] = { target, attr_name, value };
        PyObject *result = CALL_FUNCTION_WITH_ARGS3(
            target_instance->in_class->cl_setattr,
            args
        );

        if (unlikely( result == NULL ))
        {
            return false;
        }

        Py_DECREF( result );

        return true;
    }
    else
    {
        int status = PyDict_SetItem(
            target_instance->in_dict,
            attr_name,
            value
        );

        if (unlikely( status != 0 ))
        {
            return false;
        }

        return true;
    }
}
#endif

NUITKA_MAY_BE_UNUSED static bool SET_ATTRIBUTE( PyObject *target, PyObject *attr_name, PyObject *value )
{
    CHECK_OBJECT( target );
    CHECK_OBJECT( attr_name );
    CHECK_OBJECT( value );

#if PYTHON_VERSION < 300
    if ( PyInstance_Check( target ) )
    {
        return SET_INSTANCE( target, attr_name, value );
    }
    else
#endif
    {
        PyTypeObject *type = Py_TYPE( target );

        if ( type->tp_setattro != NULL )
        {
            int status = (*type->tp_setattro)( target, attr_name, value );

            if (unlikely( status == -1 ))
            {
                return false;
            }
        }
        else if ( type->tp_setattr != NULL )
        {
            int status = (*type->tp_setattr)( target, (char *)Nuitka_String_AsString_Unchecked( attr_name ), value );

            if (unlikely( status == -1 ))
            {
                return false;
            }
        }
        else if ( type->tp_getattr == NULL && type->tp_getattro == NULL )
        {
            PyErr_Format(
                PyExc_TypeError,
                "'%s' object has no attributes (assign to %s)",
                type->tp_name,
                Nuitka_String_AsString_Unchecked( attr_name )
            );

            return false;
        }
        else
        {
            PyErr_Format(
                PyExc_TypeError,
                "'%s' object has only read-only attributes (assign to %s)",
                type->tp_name,
                Nuitka_String_AsString_Unchecked( attr_name )
            );

            return false;
        }
    }

    return true;
}

NUITKA_MAY_BE_UNUSED static bool SET_ATTRIBUTE_DICT_SLOT( PyObject *target, PyObject *value )
{
    CHECK_OBJECT( target );
    CHECK_OBJECT( value );

#if PYTHON_VERSION < 300
    if ( likely( PyInstance_Check( target ) ))
    {
        PyInstanceObject *target_instance = (PyInstanceObject *)target;

        /* Note seems this doesn't have to be an exact dictionary. */
        if (unlikely( !PyDict_Check( value ) ))
        {
            PyErr_SetString( PyExc_TypeError, "__dict__ must be set to a dictionary" );
            return false;
        }

        PyObject *old = target_instance->in_dict;

        Py_INCREF( value );
        target_instance->in_dict = value;

        Py_DECREF( old );
    }
    else
#endif
    {
        PyTypeObject *type = Py_TYPE( target );

        if ( type->tp_setattro != NULL )
        {
            int status = (*type->tp_setattro)( target, const_str_plain___dict__, value );

            if (unlikely( status == -1 ))
            {
                return false;
            }
        }
        else if ( type->tp_setattr != NULL )
        {
            int status = (*type->tp_setattr)( target, (char *)"__dict__", value );

            if (unlikely( status == -1 ))
            {
                return false;
            }
        }
        else if ( type->tp_getattr == NULL && type->tp_getattro == NULL )
        {
            PyErr_Format(
                PyExc_TypeError,
                "'%s' object has no attributes (assign to __dict__)",
                type->tp_name
            );

            return false;
        }
        else
        {
            PyErr_Format(
                PyExc_TypeError,
                "'%s' object has only read-only attributes (assign to __dict__)",
                type->tp_name
            );

            return false;
        }
    }

    return true;
}

NUITKA_MAY_BE_UNUSED static bool SET_ATTRIBUTE_CLASS_SLOT( PyObject *target, PyObject *value )
{
    CHECK_OBJECT( target );
    CHECK_OBJECT( value );

#if PYTHON_VERSION < 300
    if (likely( PyInstance_Check( target ) ))
    {
        PyInstanceObject *target_instance = (PyInstanceObject *)target;

        if (unlikely( !PyClass_Check( value ) ))
        {
            PyErr_SetString( PyExc_TypeError, "__class__ must be set to a class" );
            return false;
        }

        PyObject *old = (PyObject *)( target_instance->in_class );
        Py_INCREF( value );
        target_instance->in_class = (PyClassObject *)value;
        Py_DECREF( old );
    }
    else
#endif
    {
        PyTypeObject *type = Py_TYPE( target );

        if ( type->tp_setattro != NULL )
        {
            int status = (*type->tp_setattro)( target, const_str_plain___class__, value );

            if (unlikely( status == -1 ))
            {
                return false;
            }
        }
        else if ( type->tp_setattr != NULL )
        {
            int status = (*type->tp_setattr)( target, (char *)"__class__", value );

            if (unlikely( status == -1 ))
            {
                return false;
            }
        }
        else if ( type->tp_getattr == NULL && type->tp_getattro == NULL )
        {
            PyErr_Format(
                PyExc_TypeError,
                "'%s' object has no attributes (assign to __class__)",
                type->tp_name
            );

            return false;
        }
        else
        {
            PyErr_Format(
                PyExc_TypeError,
                "'%s' object has only read-only attributes (assign to __class__)",
                type->tp_name
            );

            return false;
        }
    }

    return true;
}

NUITKA_MAY_BE_UNUSED static PyObject *LOOKUP_SPECIAL( PyObject *source, PyObject *attr_name )
{
#if PYTHON_VERSION < 300
    if ( PyInstance_Check( source ) )
    {
        return LOOKUP_INSTANCE( source, attr_name );
    }
#endif

    // TODO: There is heavy optimization in CPython to avoid it. Potentially
    // that's worth it to imitate that.

    PyObject *result = _PyType_Lookup( Py_TYPE( source ), attr_name );

    if (likely( result ))
    {
        descrgetfunc func = Py_TYPE( result )->tp_descr_get;

        if ( func == NULL )
        {
            Py_INCREF( result );
            return result;
        }
        else
        {
            PyObject *func_result = func( result, source, (PyObject *)( Py_TYPE( source ) ) );

            if (unlikely( func_result == NULL ))
            {
                return NULL;
            }

            CHECK_OBJECT( func_result );
            return func_result;
        }
    }

    PyErr_SetObject( PyExc_AttributeError, attr_name );
    return NULL;
}

#endif
