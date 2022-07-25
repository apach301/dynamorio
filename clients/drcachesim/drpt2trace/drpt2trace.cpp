/* **********************************************************
 * Copyright (c) 2022 Google, Inc.  All rights reserved.
 * **********************************************************/

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Google, Inc. nor the names of its contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL VMWARE, INC. OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/* DrPT2Trace.c
 * Command-line tool for decoding a PT trace, and converting it into a memtrace composed
 * of trace_entry_t records. This is a standalone client and not part of the
 * drmemtrace/drcachesim's workflow. It is used to convert the PT trace output by the
 * "perf record" command.
 * TODO i#5505: Currently, it only counts and prints the instruction count in the trace
 * data.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <memory>

#include "droption.h"
#include "pt2ir.h"

#define CLIENT_NAME "drpt2trace"
#define SUCCESS 0
#define FAILURE 1

#if !defined(X86_64) && !defined(LINUX)
#    error "This is only for Linux x86_64."
#endif

/***************************************************************************
 * Options
 */

/* A collection of options. */
static droption_t<bool> op_help(DROPTION_SCOPE_FRONTEND, "help", false,
                                "Print this message", "Prints the usage message.");
static droption_t<bool> op_print_trace(DROPTION_SCOPE_FRONTEND, "print_trace", false,
                                       "Print trace",
                                       "Print the disassemble code of the trace.");

static droption_t<std::string>
    op_raw_pt(DROPTION_SCOPE_FRONTEND, "raw_pt", "",
              "[Required] Path to the PT raw trace file",
              "Specifies the file path of the PT raw trace. Please run the "
              "libipt/script/perf-read-aux.bash script to get PT raw trace file from the "
              "data generated by the perf record command.");
static droption_t<std::string>
    op_elf(DROPTION_SCOPE_FRONTEND, "elf", "", "[Optional] Path to the elf file",
           "Specifies the file path of the elf file. This must be specified when "
           "converting traces that don't contain sideband information. e.g. kernel-only "
           "traces and short user traces.");
static droption_t<unsigned long long> op_elf_base(
    DROPTION_SCOPE_FRONTEND, "elf_base", 0x0,
    "[Optional] The runtime load address of the elf file",
    "This is an optional option in ELF Mode. Specifies the runtime load address of "
    "the elf file. For kernel cases, this always should be 0x0, so it is not required. "
    "But if -elf specified file's runtime load address is not 0x0, it must be set.");

static droption_t<std::string>
    op_primary_sb(DROPTION_SCOPE_FRONTEND, "primary_sb", "",
                  "[Optional] Path to primary sideband stream file",
                  "Specifies the file path of the primary sideband stream. A primary "
                  "sideband file is directly related to the trace.  For example, it may "
                  "contain the sideband information for the traced cpu. Please run the "
                  "libipt/script/perf-read-sideband.bash script to get PT sideband file "
                  "from the data generated by the perf record command. This must be "
                  "specified when converting traces that the instruction bytes are "
                  "located in multiple images. e.g., the traces of the application that "
                  "load and unload images dynamically. ");
static droption_t<std::string> op_secondary_sb(
    DROPTION_SCOPE_FRONTEND, "secondary_sb", DROPTION_FLAG_ACCUMULATE, "",
    "[Optional] Path to secondary sideband stream file",
    "This is an optional option in SIDEBAND Mode. Specifies the file path of the "
    "secondary sideband stream. A secondary sideband file "
    "is indirectly related to the trace.  For example, it may contain the sideband "
    "information for other cpus on the system. Please "
    "run the libipt/script/perf-read-sideband.bash script to get PT "
    "sideband file from the data generated by the perf record command.");
static droption_t<std::string>
    op_kcore(DROPTION_SCOPE_FRONTEND, "kcore", "", "[Optional] Path to kcore file",
             "This is an optional option in SIDEBAND Mode. Specifies the file path of "
             "kernel's core dump file. To get the kcore file, "
             "please use 'perf record --kcore' to record PT raw trace.");

/* Below options are required by the libipt and libipt-sb.
 * XXX: We should use a config file to specify these options and parse the file in pt2ir.
 */
static droption_t<int> op_pt_cpu_family(
    DROPTION_SCOPE_FRONTEND, "pt_cpu_family", 0,
    "[libipt Required] set cpu family for PT raw trace",
    "Set cpu family to the given value. Please run the "
    "libipt/script/perf-get-opts.bash script to get the value of this option "
    "from the data generated by the perf record command.");
static droption_t<int> op_pt_cpu_model(
    DROPTION_SCOPE_FRONTEND, "pt_cpu_model", 0,
    "[libipt Required] set cpu model for PT raw trace",
    "Set cpu model to the given value. Please run the "
    "libipt/script/perf-get-opts.bash script to get the value of this option "
    "from the data generated by the perf record command.");
static droption_t<int> op_pt_cpu_stepping(
    DROPTION_SCOPE_FRONTEND, "pt_cpu_stepping", 0,
    "[libipt Required] set cpu stepping for PT raw trace",
    "Set cpu stepping to the given value. Please run the "
    "libipt/script/perf-get-opts.bash script to get the value of this option "
    "from the data generated by the perf record command.");
static droption_t<int>
    op_pt_mtc_freq(DROPTION_SCOPE_FRONTEND, "pt_mtc_freq", 0,
                   "[libipt Required] set mtc frequency for PT raw trace",
                   "Set mtc frequency to the given value. Please run the "
                   "libipt/script/perf-get-opts.bash script to get the value of this "
                   "option from the data generated by the perf record command.");
static droption_t<int>
    op_pt_nom_freq(DROPTION_SCOPE_FRONTEND, "pt_nom_freq", 0,
                   "[libipt Required] set nom frequency for PT raw trace",
                   "Set nom frequency to the given value. Please run the "
                   "libipt/script/perf-get-opts.bash script to get the value of this "
                   "option from the data generated by the perf record command.");
static droption_t<int> op_pt_cpuid_0x15_eax(
    DROPTION_SCOPE_FRONTEND, "pt_cpuid_0x15_eax", 0,
    "[libipt Required] set the value of cpuid[0x15].eax for PT raw trace",
    "Set the value of cpuid[0x15].eax to the given value. Please run the "
    "libipt/script/perf-get-opts.bash script to get the value of this option from the "
    "data generated by the perf record command.");
static droption_t<int> op_pt_cpuid_0x15_ebx(
    DROPTION_SCOPE_FRONTEND, "pt_cpuid_0x15_ebx", 0,
    "[libipt Required] set the value of cpuid[0x15].ebx for PT raw trace",
    "Set the value of cpuid[0x15].ebx to the given value. Please run the "
    "libipt/script/perf-get-opts.bash script to get the value of this option from the "
    "data generated by the perf record command.");
static droption_t<std::string>
    op_sb_sysroot(DROPTION_SCOPE_FRONTEND, "sb_sysroot", "",
                  "[libipt-sb Optional] set sysroot for sideband stream",
                  "Set sysroot to the given value. Please run the "
                  "libipt/script/perf-get-opts.bash script to get the value of this "
                  "option from the data generated by the perf record command.");
static droption_t<unsigned long long>
    op_sb_sample_type(DROPTION_SCOPE_FRONTEND, "sb_sample_type", 0x0,
                      "[libipt-sb Required] set sample type for sideband stream",
                      "Set sample type to the given value(the given value must be a "
                      "hexadecimal integer and default: 0x0). Please run the "
                      "libipt/script/perf-get-opts.bash script to get the value of this "
                      "option from the data generated by the perf record command.");
static droption_t<unsigned long long>
    op_sb_time_zero(DROPTION_SCOPE_FRONTEND, "sb_time_zero", 0,
                    "[libipt-sb Required] set time zero for sideband stream",
                    "Set perf_event_mmap_page.time_zero to the given value. Please run "
                    "the libipt/script/perf-get-opts.bash script to get the value of "
                    "this option from the data generated by the perf record command.");
static droption_t<unsigned int>
    op_sb_time_shift(DROPTION_SCOPE_FRONTEND, "sb_time_shift", 0,
                     "[libipt-sb Required] set time shift for sideband stream",
                     "Set perf_event_mmap_page.time_shift to the given value. Please run "
                     "the libipt/script/perf-get-opts.bash script to get the value of "
                     "this option from the data generated by the perf record command.");
static droption_t<unsigned int>
    op_sb_time_mult(DROPTION_SCOPE_FRONTEND, "sb_time_mult", 1,
                    "[libipt-sb Required] set time mult for sideband stream",
                    "Set perf_event_mmap_page.time_mult to the given value. Please run "
                    "the libipt/script/perf-get-opts.bash script to get the value of "
                    "this option from the data generated by the perf record command.");
static droption_t<unsigned long long>
    op_sb_tsc_offset(DROPTION_SCOPE_FRONTEND, "sb_tsc_offset", 0x0,
                     "[libipt-sb Optional] set tsc offset for sideband stream",
                     "Set perf events the given value ticks earlier(the given value "
                     "must be a hexadecimal integer and default: 0x0). Please run the "
                     "libipt/script/perf-get-opts.bash script to get the value of this "
                     "option from the data generated by the perf record command.");
static droption_t<unsigned long long> op_sb_kernel_start(
    DROPTION_SCOPE_FRONTEND, "sb_kernel_start", 0x0,
    "[libipt-sb Optional] set kernel start for sideband stream",
    "Set the start address of the kernel to the given value(the "
    "given value must be a hexadecimal integer and default: 0x0). Please run the "
    "libipt/script/perf-get-opts.bash script to get the value of this option from the "
    "data generated by the perf record command.");

/****************************************************************************
 * Output
 */

static void
print_results(IN instrlist_t *ilist)
{
    instr_t *instr = instrlist_first(ilist);
    uint64_t count = 0;
    while (instr != NULL) {
        count++;
        instr = instr_get_next(instr);
    }

    if (op_print_trace.specified()) {
        /* Print the disassemble code of the trace. */
        instrlist_disassemble(GLOBAL_DCONTEXT, 0, ilist, STDOUT);
    }
    std::cout << "Number of Instructions: " << count << std::endl;
}

/****************************************************************************
 * Options Handling
 */

static void
print_usage()
{
    std::cerr
        << CLIENT_NAME
        << ": Command-line tool that decodes the given PT raw trace and returns the "
           "outputs as specified by given flags."
        << std::endl;
    std::cerr << "Usage: " << CLIENT_NAME << " [options]" << std::endl;
    std::cerr << droption_parser_t::usage_short(DROPTION_SCOPE_FRONTEND) << std::endl;
}

static bool
option_init(int argc, const char *argv[])
{
    std::string parse_err;
    if (!droption_parser_t::parse_argv(DROPTION_SCOPE_FRONTEND, argc, argv, &parse_err,
                                       NULL)) {
        std::cerr << CLIENT_NAME << "Usage error: " << parse_err << std::endl;
        print_usage();
        return false;
    }
    if (op_help.specified()) {
        print_usage();
        return false;
    }
    std::vector<droption_parser_t *> required_op_list;
    required_op_list.push_back(&op_raw_pt);
    required_op_list.push_back(&op_pt_cpu_family);
    required_op_list.push_back(&op_pt_cpu_model);
    required_op_list.push_back(&op_pt_cpu_stepping);
    required_op_list.push_back(&op_pt_mtc_freq);
    required_op_list.push_back(&op_pt_nom_freq);
    required_op_list.push_back(&op_pt_cpuid_0x15_eax);
    required_op_list.push_back(&op_pt_cpuid_0x15_ebx);

    for (auto &op : required_op_list) {
        if (!op->specified()) {
            std::cerr << CLIENT_NAME << ": option " << op->get_name() << " is required."
                      << std::endl;
            print_usage();
            return false;
        }
    }

    /* Because Intel PT doesn't save instruction bytes or memory contents, the converter
     * needs the instruction bytes for each IPs (Instruction Pointers) to decode the PT
     * trace.
     * drpt2trace supports two modes to convert:
     * (1) Sideband Mode: the user must provide sideband data and parameters. In this
     * mode, the converter uses sideband decoders to simulate image switches during the
     * conversion. For example, we can use this mode to convert the traces where the
     * instruction bytes are located in multiple images.
     * (2) Elf Mode: the user needs to provide an elf image for the PT trace. This mode is
     * for cases where the PT trace's instruction bytes belong to one image. So we can use
     * this mode to convert the kernel trace and the short-term user trace where it's
     * likely that we'll not have an image switch.
     */
    if ((!op_elf.specified() && !op_primary_sb.specified()) ||
        (op_elf.specified() && op_primary_sb.specified())) {
        std::cerr << CLIENT_NAME
                  << ": exactly one of the options --elf, --primary_sb must be specified."
                  << std::endl;
        print_usage();
        return false;
    }

    /* Check if the required options for sideband mode are specified. */
    if (op_primary_sb.specified()) {
        std::vector<droption_parser_t *> sb_required_op_list;
        sb_required_op_list.push_back(&op_sb_sample_type);
        sb_required_op_list.push_back(&op_sb_time_zero);
        sb_required_op_list.push_back(&op_sb_time_shift);
        sb_required_op_list.push_back(&op_sb_time_mult);
        for (auto &op : sb_required_op_list) {
            if (!op->specified()) {
                std::cerr << CLIENT_NAME << ": option " << op->get_name()
                          << " is required in sideband mode." << std::endl;
                print_usage();
                return false;
            }
        }
    }
    return true;
}
/****************************************************************************
 * Main Function
 */

int
main(int argc, const char *argv[])
{
    /* Parse the command line. */
    if (!option_init(argc, argv)) {
        return FAILURE;
    }

    pt2ir_config_t config = {};
    config.raw_file_path = op_raw_pt.get_value();
    config.elf_file_path = op_elf.get_value();
    config.elf_base = op_elf_base.get_value();
    config.sb_primary_file_path = op_primary_sb.get_value();
    std::istringstream op_secondary_sb_stream(op_secondary_sb.get_value());
    copy(std::istream_iterator<std::string>(op_secondary_sb_stream),
         std::istream_iterator<std::string>(),
         std::back_inserter(config.sb_secondary_file_path_list));
    config.kcore_path = op_kcore.get_value();

    config.pt_config.cpu.family = op_pt_cpu_family.get_value();
    config.pt_config.cpu.model = op_pt_cpu_model.get_value();
    config.pt_config.cpu.stepping = op_pt_cpu_stepping.get_value();
    if (op_pt_cpu_family.get_value() != 0) {
        config.pt_config.cpu.vendor = CPU_VENDOR_INTEL;
    } else {
        config.pt_config.cpu.vendor = CPU_VENDOR_UNKNOWN;
    }
    config.pt_config.cpuid_0x15_eax = op_pt_cpuid_0x15_eax.get_value();
    config.pt_config.cpuid_0x15_ebx = op_pt_cpuid_0x15_ebx.get_value();
    config.pt_config.mtc_freq = op_pt_mtc_freq.get_value();
    config.pt_config.nom_freq = op_pt_nom_freq.get_value();
    config.sb_config.sample_type = op_sb_sample_type.get_value();
    config.sb_config.sysroot = op_sb_sysroot.get_value();
    config.sb_config.time_zero = op_sb_time_zero.get_value();
    config.sb_config.time_shift = op_sb_time_shift.get_value();
    config.sb_config.time_mult = op_sb_time_mult.get_value();
    config.sb_config.tsc_offset = op_sb_tsc_offset.get_value();
    config.sb_config.kernel_start = op_sb_kernel_start.get_value();

    /* Convert the PT raw trace to DR IR. */
    std::unique_ptr<pt2ir_t> ptconverter(new pt2ir_t());
    if (!ptconverter->init(config)) {
        std::cerr << CLIENT_NAME << ": failed to initialize pt2ir_t." << std::endl;
        return FAILURE;
    }
    instrlist_t *ilist = nullptr;
    pt2ir_convert_status_t status = ptconverter->convert(&ilist);
    if (status != PT2IR_CONV_SUCCESS) {
        std::cerr << CLIENT_NAME << ": failed to convert PT raw trace to DR IR."
                  << "[error status: " << status << "]" << std::endl;
        return FAILURE;
    }

    /* Print the count and the disassemble code of DR IR. */
    print_results(ilist);

    instrlist_clear_and_destroy(GLOBAL_DCONTEXT, ilist);
    return SUCCESS;
}
