# Copyright © 2024 Advanced Micro Devices, Inc. All rights reserved.

import argparse
import os
from pathlib import Path
import zlib
import fnmatch

def get_var_name(file_path):
    file_name = file_path.name
    dir_name = file_path.parent.name
    var_name = dir_name + '_' +  file_name.split('.')[0]
    return var_name

def bin_to_cpp(file_path, txn_hdrf, txn_srcf):

    variable_name = get_var_name(file_path)
    decompressed_size = 0

    with open(file_path, 'rb') as file:
        binary_data = file.read()
        decompressed_size = len(binary_data)
        compressed_data = zlib.compress(binary_data)
        hex_data = ''.join(f'\\x{byte:02x}' for byte in compressed_data)
        # Windows has a 16380-char string limit. Break up long strings with ""
        max_string_len = 16380
        hex_data = "\"\"".join(hex_data[i:i+max_string_len] for i in range(0, len(hex_data), max_string_len))

    with open(txn_hdrf, "a") as hdr:
        hdr.write(f'const std::string& get{variable_name.capitalize()}();\n')

    with open(txn_srcf, "a") as src:
        src.write(f'static char {variable_name}[] = "{hex_data}";\n')
        # write a function
        src.write(f"static std::string initialize_{variable_name}()" + "{\n")
        src.write("std::string ret;\n")
        src.write("uLongf ret_size = 0;\n")
        src.write(f"ret.resize({decompressed_size});\n")
        src.write("z_stream infstream;\n")
        src.write("infstream.zalloc = Z_NULL;\n")
        src.write("infstream.zfree = Z_NULL;\n")
        src.write("infstream.opaque = Z_NULL;\n")
        src.write(f"infstream.avail_in = {len(compressed_data)};\n")
        src.write(f"infstream.next_in = reinterpret_cast<Bytef*>({variable_name});\n")
        src.write(f"infstream.avail_out = {decompressed_size};\n")
        src.write(f"infstream.next_out = reinterpret_cast<Bytef*>(&ret[0]);\n")
        src.write("inflateInit(&infstream);\n")
        src.write("inflate(&infstream, Z_NO_FLUSH);\n")
        src.write("inflateEnd(&infstream);\n")
        src.write("return ret;\n")
        src.write("}\n")

        src.write(f'const std::string& get{variable_name.capitalize()}() {{\n')
        src.write(f'static const std::string {variable_name}_str = initialize_{variable_name}();\n')
        src.write(f'return {variable_name}_str;\n')
        src.write('}\n')

def write_transaction_src(out_dir, txn_list, quiet, txn_hdr):
    with open(Path(out_dir) / Path('transaction.cpp'), 'w') as txn_src:
        txn_src.write('#include "txn_container.hpp"\n')
        txn_src.write(f'#include "{txn_hdr}"\n')
        txn_src.write('\nTransaction::Transaction(){{}};\n')
        txn_src.write('const std::string& Transaction::get_txn_str(const std::string& name) {\n')
        txn_src.write('\tstd::string op_name;\n')
        txn_src.write('\top_name = name;\n')
        for txn_file in txn_list:
            var_name = get_var_name(txn_file)
            func_name = get_var_name(txn_file).capitalize()
            txn_src.write(f'\tif (op_name == "{var_name}")\n')
            txn_src.write(f'\t\treturn get{func_name}();\n')
        txn_src.write('\telse\n')
        txn_src.write('\t\tthrow std::runtime_error("Invalid transaction binary string: " + op_name);\n')
        txn_src.write('}\n')

        filtering_words = ["param", "ddr_buffer_info"] #getting rid of repetition
        filtering_fun = lambda x: all([not substr in get_var_name(x) for substr in filtering_words])
        txn_list_filtered = list(filter(filtering_fun, txn_list))
        txn_src.write('std::vector<std::string> Transaction::match_prefix(const std::string& prefix) {\n')
        txn_src.write(f'\tconst static std::array<std::string, {len(txn_list_filtered)}> all_txn_strings =\u007b\n')
        for i, txn_file in enumerate(txn_list_filtered):
            var_name = get_var_name(txn_file)
            if i == len(txn_list) - 1 :
                txn_src.write(f'\t"{var_name}"\n')
            else:
                txn_src.write(f'\t"{var_name}",\n')
        txn_src.write('\t};\n')
        txn_src.write('\tstd::vector<std::string> matched_strings;\n')
        txn_src.write('\tstd::copy_if(all_txn_strings.begin(), all_txn_strings.end(), std::back_inserter(matched_strings),\n')
        txn_src.write('\t\t[prefix](const std::string& txn_var_name){ return txn_var_name.rfind(prefix, 0) == 0; });\n')
        txn_src.write('\treturn matched_strings;\n')
        txn_src.write('}\n')
        if not quiet:
            print('transaction.cpp is generated')

def get_bin_file():
    for root, dirs, files in os.walk(tr_path):
        for filename in fnmatch.filter(files, '*.bin'):
            yield Path(root) / filename
        for filename in fnmatch.filter(files, '*.json'):
            yield Path(root) / filename

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default="", type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--disable-large-txn-ops", action="store_true")
    args = parser.parse_args()

    large_txn_ops = []
    if args.disable_large_txn_ops:
        large_txn_ops = ["/mladfmatmulbias/", "/elwmul/"]

    root_dir = args.root or Path(os.environ.get('DD_ROOT'))
    out_dir = args.out_dir

    tr_path = root_dir / Path("transaction/stx")

    txn_hdr_fname = "all_txn_pkg.hpp"
    txn_src_fname = "all_txn_pkg.cpp"

    if args.list:
        print("transaction.cpp")
        print(txn_src_fname)
    else:
        if not os.path.exists(out_dir):
            os.mkdir(out_dir)
        txn_list=[]
        txn_hdrf = Path(out_dir) / Path(txn_hdr_fname)
        txn_srcf = Path(out_dir) / Path(txn_src_fname)
        with open(txn_srcf, "w") as src:
            src.write('#include <string>\n')
            src.write('#include <zlib.h>\n')
            src.write(f'#include "{txn_hdr_fname}"\n')

        with open(txn_hdrf, "w") as hdr:
            hdr.write('#pragma once\n')
            hdr.write('#include <string>\n')
            hdr.write('#include <tuple>\n')

        for filepath in get_bin_file():
            fname = str(filepath)
            fname = fname.replace("\\", "/")
            if any(map(lambda large_op: large_op in fname, large_txn_ops)):
                continue
            bin_to_cpp(filepath, txn_hdrf, txn_srcf)
            txn_list.append(filepath)
        write_transaction_src(out_dir, txn_list, args.quiet, txn_hdr_fname)
        if not args.quiet:
            print(f"Header generated: {txn_hdr_fname}")
            print(f"Source generated: {txn_src_fname}")