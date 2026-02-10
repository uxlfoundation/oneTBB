#!/usr/bin/env python3
"""
Post-process SWIG-generated api_wrap.cpp to add free-threading (NOGIL) support.

This script modifies the PyModuleDef structure to use multi-phase initialization
with Py_mod_gil slot, declaring that the module can run safely without the GIL.

For Python 3.13+ free-threading, the module must declare Py_MOD_GIL_NOT_USED
in its PyModuleDef_Slot array BEFORE module creation, not after.

Usage:
    python patch_nogil.py api_wrap.cpp
"""

import re
import sys
import os

PATCHED_MARKER = "/* NOGIL_PATCHED_V2 */"


def patch_file(filepath: str) -> bool:
    """
    Patch the SWIG-generated wrapper file to add NOGIL support.
    
    Returns True if file was modified, False if already patched.
    """
    with open(filepath, 'r') as f:
        content = f.read()
    
    # Check if already patched with v2
    if PATCHED_MARKER in content:
        print(f"{filepath}: Already patched for NOGIL support (v2)")
        return False
    
    # Remove any previous patch markers
    content = re.sub(r'/\* NOGIL_PATCHED \*/\n?', '', content)
    content = re.sub(r'#if PY_VERSION_HEX >= 0x030D0000\s*\n\s*/\* Declare this module.*?#endif\n?', '', content, flags=re.DOTALL)
    
    # Find the PyModuleDef structure - SWIG generates something like:
    # static struct PyModuleDef SWIG_module = {
    #   PyModuleDef_HEAD_INIT,
    #   "api",
    #   NULL,
    #   ...
    # };
    
    # We need to add m_slots field to the PyModuleDef
    # The structure is: {HEAD_INIT, name, doc, size, methods, slots, traverse, clear, free}
    
    # First, add the slots array definition before PyModuleDef
    slots_code = '''
/* Free-threading (NOGIL) Python 3.13+ support - multi-phase init */
#if PY_VERSION_HEX >= 0x030D0000
static PyModuleDef_Slot tbb_api_slots[] = {
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
    {0, NULL}
};
#endif

'''
    
    # Find where PyModuleDef is defined
    moduledef_match = re.search(
        r'(static struct PyModuleDef SWIG_module\s*=\s*\{)',
        content
    )
    
    if not moduledef_match:
        print(f"{filepath}: Could not find PyModuleDef structure")
        return False
    
    insert_pos = moduledef_match.start()
    content = content[:insert_pos] + PATCHED_MARKER + "\n" + slots_code + content[insert_pos:]
    
    # Now modify the PyModuleDef to include m_slots
    # SWIG typically generates:
    #   static struct PyModuleDef SWIG_module = {
    #     PyModuleDef_HEAD_INIT,
    #     "api",
    #     NULL,
    #     -1,
    #     SwigMethods,
    #     NULL,  <- this is m_slots, we need to set it conditionally
    #     NULL,
    #     NULL,
    #     NULL
    #   };
    
    # Find the m_slots field (6th field, after methods)
    # Pattern: look for the structure and find NULL after SwigMethods
    moduledef_pattern = r'(static struct PyModuleDef SWIG_module\s*=\s*\{[^}]*SwigMethods,\s*)\n\s*NULL,'
    
    replacement = r'''\1
#if PY_VERSION_HEX >= 0x030D0000
    tbb_api_slots,  /* m_slots - for free-threading support */
#else
    NULL,  /* m_slots */
#endif'''
    
    content = re.sub(moduledef_pattern, replacement, content, count=1)
    
    with open(filepath, 'w') as f:
        f.write(content)
    
    print(f"{filepath}: Patched for NOGIL support (v2 - multi-phase init)")
    return True


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <api_wrap.cpp>")
        sys.exit(1)
    
    filepath = sys.argv[1]
    
    if not os.path.exists(filepath):
        print(f"Error: {filepath} not found")
        sys.exit(1)
    
    patched = patch_file(filepath)
    sys.exit(0)


if __name__ == "__main__":
    main()
