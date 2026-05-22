#!/usr/bin/env python3
# Copyright 2020-2023, Collabora, Ltd.
# Copyright 2025, Marko Kazimirovic <kazimirovicmarko@photon.me>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate code from a JSON file describing the IPC protocol."""

import argparse
import os.path
from string import Template

from ipcproto.common import (Proto, write_decl, write_invocation,
                             write_result_handler, write_cpp_header_guard_start,
                             write_cpp_header_guard_end, write_msg_struct,
                             write_reply_struct, write_msg_send)

header = '''// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: GPL-3.0-or-later
/*!
 * @file
 * @brief  {brief}.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc{suffix}
 */
'''


def write_send_definition(f, call):
    """Write a ipc_send_CALLNAME_locked function."""
    call.write_send_decl(f)
    f.write("\n{\n")
    f.write("\tIPC_TRACE(ipc_c, \"Sending " + call.name + "\");\n\n")

    write_msg_struct(f, call, '\t')

    write_msg_send(f, 'xrt_result_t ret', indent="\t")

    f.write("\n\treturn ret;\n}\n")


def write_receive_definition(f, call):
    """Write a ipc_receive_CALLNAME_locked function."""
    call.write_receive_decl(f)
    f.write("\n{\n")
    f.write("\tIPC_TRACE(ipc_c, \"Receiving " + call.name + "\");\n\n")

    write_reply_struct(f, call, '\t')

    f.write("\n\t// Await the reply")
    func = 'ipc_receive'
    args = ['&ipc_c->imc', '&_reply', 'sizeof(_reply)']
    write_invocation(f, 'xrt_result_t ret', func, args, indent="\t")
    f.write(";")
    write_result_handler(f, 'ret', None, indent="\t")

    for arg in call.out_args:
        f.write("\t*out_" + arg.name + " = _reply." + arg.name + ";\n")

    f.write("\n\treturn _reply.result;\n}\n")


def write_call_definition(f, call):
    """Write a ipc_call_CALLNAME function."""
    call.write_call_decl(f)
    f.write("\n{\n")

    f.write("\tIPC_TRACE(ipc_c, \"Calling " + call.name + "\");\n\n")

    write_msg_struct(f, call, '\t')
    write_reply_struct(f, call, '\t')

    f.write("""
\t// Other threads must not read/write the fd while we wait for reply
\tos_mutex_lock(&ipc_c->mutex);
""")
    cleanup = "os_mutex_unlock(&ipc_c->mutex);"

    # Prepare initial sending
    write_msg_send(f, 'xrt_result_t ret', indent="\t")
    write_result_handler(f, 'ret', cleanup, indent="\t")

    if call.in_handles:
        f.write("\n\t// Send our handles separately\n")
        f.write("\n\t// Wait for server sync")
        # Must sync with the server so it's expecting the next message.
        write_invocation(
            f,
            'ret',
            'ipc_receive',
            (
                '&ipc_c->imc',
                '&_sync',
                'sizeof(_sync)'
                ),
            indent="\t"
        )
        f.write(';')
        write_result_handler(f, 'ret', cleanup, indent="\t")

        # Must send these in a second message
        # since the server doesn't know how many to expect.
        f.write("\n\t// We need this message data as filler only\n")
        f.write("\tstruct ipc_command_msg _handle_msg = {\n")
        f.write("\t    .cmd = " + str(call.id) + ",\n")
        f.write("\t};\n")
        write_invocation(
            f,
            'ret',
            'ipc_send_handles_' + call.in_handles.stem,
            (
                '&ipc_c->imc',
                "&_handle_msg",
                "sizeof(_handle_msg)",
                call.in_handles.arg_name,
                call.in_handles.count_arg_name
            ),
            indent="\t"
        )
        f.write(';')
        write_result_handler(f, 'ret', cleanup, indent="\t")

    f.write("\n\t// Await the reply")
    func = 'ipc_receive'
    args = ['&ipc_c->imc', '&_reply', 'sizeof(_reply)']
    if call.out_handles:
        func += '_handles_' + call.out_handles.stem
        args.extend(call.out_handles.arg_names)
    write_invocation(f, 'ret', func, args, indent="\t")
    f.write(';')
    write_result_handler(f, 'ret', cleanup, indent="\t")

    for arg in call.out_args:
        f.write("\t*out_" + arg.name + " = _reply." + arg.name + ";\n")
    f.write("\n\t" + cleanup)
    f.write("\n\treturn _reply.result;\n}\n")


def generate_h(file, p):
    """Generate protocol header.

    Defines command enum, utility functions, and command and reply structures.
    """
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))
    template_file = os.path.join(script_dir, 'ipc_protocol_generated.h.template')

    # Goes directly into the template file.
    ipc_commands = '\n'.join([f'\t{call.id},' for call in p.calls])

    # Goes directly into the template file.
    ipc_cmd_cases = '\n'.join([f'\tcase {call.id}: return "{call.id}";' for call in p.calls])

    # Build message and reply structs.
    ipc_msg_structs_list = []
    for call in p.calls:
        # Should we emit a msg struct.
        if call.needs_msg_struct:
            struct_lines = [f'struct ipc_{call.name}_msg']
            struct_lines.append('{')
            struct_lines.append('\tenum ipc_command cmd;')
            for arg in call.in_args:
                struct_lines.append('\t' + arg.get_struct_field() + ';')
            if call.in_handles:
                struct_lines.append('\t%s %s;' % (call.in_handles.count_arg_type,
                                                  call.in_handles.count_arg_name))
            struct_lines.append('};')

            # Each entry contains a struct complete struct declaration.
            ipc_msg_structs_list.append('\n'.join(struct_lines))

        # Should we emit a reply struct.
        if call.out_args:
            struct_lines = [f'struct ipc_{call.name}_reply']
            struct_lines.append('{')
            struct_lines.append('\txrt_result_t result;')
            for arg in call.out_args:
                struct_lines.append('\t' + arg.get_struct_field() + ';')
            struct_lines.append('};')

            # Each entry contains a struct complete struct declaration.
            ipc_msg_structs_list.append('\n'.join(struct_lines))

    # What finally goes into the template file.
    # The struct declarations doesn't end on a newline,
    # so insert two for each declaration.
    ipc_msg_structs = '\n\n'.join(ipc_msg_structs_list)

    # Read the template file.
    with open(template_file, 'r') as f:
        template = Template(f.read())

    # Substitute values into the template.
    filled = template.substitute(
        ipc_commands=ipc_commands,
        ipc_cmd_cases=ipc_cmd_cases,
        ipc_msg_structs=ipc_msg_structs
    )

    # Write the generated header file.
    with open(file, 'w') as f:
        f.write(filled)


def generate_client_c(file, p):
    """Generate IPC client proxy source."""
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC client code', suffix='_client'))
    f.write('''
#include "client/ipc_client.h"
#include "ipc_protocol_generated.h"


\n''')

    # Loop over all of the calls.
    for call in p.calls:
        if call.varlen:
            write_send_definition(f, call)
            write_receive_definition(f, call)
        else:
            write_call_definition(f, call)

    f.close()


def generate_client_h(file, p):
    """Generate IPC client header.

    Contains prototypes for generated IPC proxy call functions.
    """
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC client code', suffix='_client'))
    f.write('''
#pragma once

#include "shared/ipc_protocol.h"
#include "ipc_protocol_generated.h"
#include "client/ipc_client.h"

''')
    write_cpp_header_guard_start(f)
    f.write("\n")

    for call in p.calls:
        if call.varlen:
            call.write_send_decl(f)
            f.write(";\n")
            call.write_receive_decl(f)
        else:
            call.write_call_decl(f)
        f.write(";\n")

    write_cpp_header_guard_end(f)
    f.close()


def generate_server_c(file, p):
    """Generate IPC server stub/dispatch source."""

    # Mapping from IPC snake_case call names to OpenXR function names.
    # Only calls that .augins module authors are expected to hook are listed.
    # Hooks fire BEFORE ipc_send so modules can modify reply (output) fields.
    # Calling convention: augins_fire_hooks(xr_name, ics, msg, &reply, NULL)
    #   a0 = volatile struct ipc_client_state * (opaque session context)
    #   a1 = IPC msg struct * -- deserialized input args  (NULL if no in-args)
    #   a2 = IPC reply struct * -- writable output args   (NULL if no out-args)
    #   a3 = NULL (reserved)
    aug_ipc_to_xr = {
        "session_create":            "xrCreateSession",
        "session_begin":             "xrBeginSession",
        "session_end":               "xrEndSession",
        "session_destroy":           "xrDestroySession",
        "session_request_exit":      "xrRequestExitSession",
        "session_poll_events":       "xrPollEvent",
        "compositor_predict_frame":  "xrWaitFrame",
        "compositor_begin_frame":    "xrBeginFrame",
        "compositor_end_frame":      "xrEndFrame",
        "swapchain_create":          "xrCreateSwapchain",
        "swapchain_destroy":         "xrDestroySwapchain",
        "swapchain_acquire_image":   "xrAcquireSwapchainImage",
        "swapchain_wait_image":      "xrWaitSwapchainImage",
        "swapchain_release_image":   "xrReleaseSwapchainImage",
        "space_locate_space":        "xrLocateSpace",
        "space_locate_spaces":       "xrLocateSpaces",
        # space_locate_device backs xrLocateViews's "head device in base space"
        # half (oxr_space_locate_device -> IPC). Same reply layout as
        # space_locate_space (struct xrt_space_relation). Note: xrLocateViews
        # ALSO calls device_get_tracked_pose for the head's own-frame pose;
        # see below.
        # space_locate_device backs xrLocateViews's T_base_xdev half on the
        # service side. Module-facing name is synthetic (the OpenXR call this
        # IPC partially backs is xrLocateViews, but xrLocateViews's full
        # signature is not available at this IPC layer -- the client already
        # decomposed it into per-device queries). Modules export
        #   XrResult aug_LocateDeviceInSpace(XrSpace, XrTime, XrSpaceLocation*)
        # The adapter filters to head-device queries only in v0.2 (see
        # PROJECT_PLAN.md v0.2.x parking lot for non-head device roles).
        "space_locate_device":       "aug_LocateDeviceInSpace",
        # device_get_tracked_pose is the OTHER half of xrLocateViews -- it
        # populates T_xdev_head (the head's pose in the xdev's tracking-origin
        # space) which then gets composed with T_base_xdev. Without hooking
        # this, gyro+accel orientation leaks into the final view orientation
        # even when ARCore overrides the locate_device reply. Synthetic name
        # since this is a per-xdev internal helper, not a direct OpenXR fn.
        "device_get_tracked_pose":   "aug_deviceGetTrackedPose",
        # Synthetic key -- no OpenXR analogue; lets modules cache the canonical
        # reference-space IDs as soon as the client requests them.
        "space_create_semantic_ids": "aug_spaceCreateSemanticIds",
    }

    # Aug-Ins v0.2 service-side dispatch: which OpenXR function names
    # have a hand-written adapter in src/xrt/augins/adapters.cpp.
    # The codegen emits the dispatch fork only for these. As more
    # adapters are added, append their names here. (Phase 2d first
    # cut: just xrLocateSpace.) Codegen-from-schema for the adapter
    # bodies themselves is a v0.2.x or v0.3 task; see PROJECT_PLAN.md.
    aug_implemented_adapters = {
        "xrLocateSpace",
        "aug_LocateDeviceInSpace",
    }

    f = open(file, "w")
    f.write(header.format(brief='Generated IPC server code', suffix='_server'))
    f.write('''
#include "xrt/xrt_limits.h"

#include "shared/ipc_protocol.h"
#include "shared/ipc_utils.h"

#include "server/ipc_server.h"

#include "ipc_server_generated.h"

#ifdef XRT_FEATURE_AUG_INS
#include "dispatch.h"
#include "adapters.h"
#endif

''')

    f.write('''
xrt_result_t
ipc_dispatch(volatile struct ipc_client_state *ics, ipc_command_t *ipc_command)
{
\tswitch (*ipc_command) {
''')

    for call in p.calls:
        f.write("\tcase " + call.id + ": {\n")

        f.write("\t\tIPC_TRACE(ics->server, \"Dispatching " + call.name +
                "\");\n\n")

        if call.needs_msg_struct:
            f.write(
                "\t\tstruct ipc_{}_msg *msg = ".format(call.name))
            f.write("(struct ipc_{}_msg *)ipc_command;\n".format(call.name))

        if call.varlen:
            f.write("\t\t// No return arguments")
        elif call.out_args:
            f.write("\t\tstruct ipc_%s_reply reply = {0};\n" % call.name)
        else:
            f.write("\t\tstruct ipc_result_reply reply = {0};\n")

        if call.in_handles:
            # We need to fetch these handles separately
            f.write("\t\tstruct ipc_result_reply _sync = {XRT_SUCCESS};\n")
            f.write("\t\t%s in_%s[XRT_MAX_IPC_HANDLES] = {0};\n" % (
                call.in_handles.typename, call.in_handles.arg_name))
            f.write("\t\tstruct ipc_command_msg _handle_msg = {0};\n")
        if call.out_handles:
            f.write("\t\t%s %s[XRT_MAX_IPC_HANDLES] = {0};\n" % (
                call.out_handles.typename, call.out_handles.arg_name))
            f.write("\t\t%s %s = {0};\n" % (
                call.out_handles.count_arg_type,
                call.out_handles.count_arg_name))
        f.write("\n")

        if call.in_handles:
            # Validate the number of handles.
            f.write("\t\tif (msg->%s > XRT_MAX_IPC_HANDLES) {\n" % (call.in_handles.count_arg_name))
            f.write("\t\t\treturn XRT_ERROR_IPC_FAILURE;\n")
            f.write("\t\t}\n")

            # Let the client know we are ready to receive the handles.
            write_invocation(
                f,
                'xrt_result_t sync_result',
                'ipc_send',
                (
                    "(struct ipc_message_channel *)&ics->imc",
                    "&_sync",
                    "sizeof(_sync)"
                ),
                indent="\t\t"
            )
            f.write(";")
            write_result_handler(f, "sync_result",
                                 indent="\t\t")
            write_invocation(
                f,
                'xrt_result_t receive_handle_result',
                'ipc_receive_handles_' + call.in_handles.stem,
                (
                    "(struct ipc_message_channel *)&ics->imc",
                    "&_handle_msg",
                    "sizeof(_handle_msg)",
                    "in_" + call.in_handles.arg_name,
                    "msg->"+call.in_handles.count_arg_name
                ),
                indent="\t\t"
            )
            f.write(";")
            write_result_handler(f, "receive_handle_result",
                                 indent="\t\t")
            f.write("\t\tif (_handle_msg.cmd != %s) {\n" % str(call.id))
            f.write("\t\t\treturn XRT_ERROR_IPC_FAILURE;\n")
            f.write("\t\t}\n")

        # Write call to ipc_handle_CALLNAME
        args = ["ics"]

        # Always provide in arguments.
        for arg in call.in_args:
            args.append(("&msg->" + arg.name)
                        if arg.is_aggregate
                        else ("msg->" + arg.name))

        # No reply arguments on varlen.
        if not call.varlen:
            args.extend("&reply." + arg.name for arg in call.out_args)

        if call.out_handles:
            args.extend(("XRT_MAX_IPC_HANDLES",
                         call.out_handles.arg_name,
                         "&" + call.out_handles.count_arg_name))

        if call.in_handles:
            args.extend(("&in_%s[0]" % call.in_handles.arg_name,
                         "msg->"+call.in_handles.count_arg_name))

        # Should we put the return in the reply or return it?
        return_target = 'reply.result'
        if call.varlen:
            return_target = 'xrt_result_t xret'

        # Aug-Ins v0.2 dispatch fork:
        # If this IPC call has an OpenXR-name mapping AND a hand-written
        # adapter exists for that name, emit a conditional call that
        # runs the adapter when modules are registered for the name,
        # otherwise the original ipc_handle_<call>. Adapters take the
        # SAME argument list as ipc_handle_<call> -- the adapter calls
        # ipc_handle_<call> itself internally to fill the baseline, then
        # iterates registered modules per the Q1 through Q5 rules.
        # See src/xrt/augins/adapters.cpp.
        #
        # Varlen calls are skipped (no clean adapter pattern for streaming).
        xr_name = aug_ipc_to_xr.get(call.name) if not call.varlen else None
        has_adapter = xr_name is not None and xr_name in aug_implemented_adapters

        if has_adapter:
            f.write('\n#ifdef XRT_FEATURE_AUG_INS\n')
            f.write('\t\tif (aug_has_modules_for("%s")) {\n' % xr_name)
            write_invocation(f, return_target, 'aug_adapter_' + call.name,
                             args, indent="\t\t\t")
            f.write(";\n")
            f.write('\t\t} else\n')
            f.write('#endif\n')
            f.write('\t\t{\n')
            write_invocation(f, return_target, 'ipc_handle_' + call.name,
                             args, indent="\t\t\t")
            f.write(";\n")
            f.write('\t\t}\n')
        else:
            write_invocation(f, return_target, 'ipc_handle_' +
                             call.name, args, indent="\t\t")
            f.write(";\n")

        # TODO do we check reply.result and
        # error out before replying if it's not success?

        if not call.varlen:
            func = 'ipc_send'
            args = ["(struct ipc_message_channel *)&ics->imc",
                    "&reply",
                    "sizeof(reply)"]
            if call.out_handles:
                func += '_handles_' + call.out_handles.stem
                args.extend(call.out_handles.arg_names)
            write_invocation(f, 'xrt_result_t xret', func, args, indent="\t\t")
            f.write(";")

        f.write("\n\t\treturn xret;\n")
        f.write("\t}\n")
    f.write('''\tdefault:
\t\tU_LOG_E("UNHANDLED IPC MESSAGE! %d", *ipc_command);
\t\treturn XRT_ERROR_IPC_FAILURE;
\t}
}

''')

    f.write('''
size_t
ipc_command_size(const enum ipc_command cmd)
{
\tswitch (cmd) {
''')

    for call in p.calls:
        if call.needs_msg_struct:
            f.write("\tcase " + call.id + ": return sizeof(struct ipc_{}_msg);\n".format(call.name))
        else:
            f.write("\tcase " + call.id + ": return sizeof(enum ipc_command);\n")

    f.write('''\tdefault:
\t\tU_LOG_E("UNHANDLED IPC COMMAND! %d", cmd);
\t\treturn 0;
\t}
}

''')

    f.write('''
const char *
ipc_command_name(const enum ipc_command cmd)
{
\tswitch (cmd) {
''')

    for call in p.calls:
        f.write('\tcase %s: return "%s";\n' % (call.id, call.name))

    f.write('''\tdefault: return NULL;
\t}
}

''')

    f.close()


def generate_server_header(file, p):
    """Generate IPC server header.

    Declares handler prototypes to implement,
    as well as the prototype for the generated dispatch function.
    """
    f = open(file, "w")
    f.write(header.format(brief='Generated IPC server code', suffix='_server'))
    f.write('''
#pragma once

#include "shared/ipc_protocol.h"
#include "ipc_protocol_generated.h"
#include "server/ipc_server.h"


''')

    write_cpp_header_guard_start(f)
    f.write("\n")

    # Those decls are constant, but we must write them here
    # because they depends on a generated enum.
    write_decl(
        f,
        "xrt_result_t",
        "ipc_dispatch",
        [
            "volatile struct ipc_client_state *ics",
            "ipc_command_t *ipc_command"
        ]
    )
    f.write(";\n")

    write_decl(
        f,
        "size_t",
        "ipc_command_size",
        [
            "const enum ipc_command cmd"
        ]
    )
    f.write(";\n")

    write_decl(
        f,
        "const char *",
        "ipc_command_name",
        [
            "const enum ipc_command cmd"
        ]
    )
    f.write(";\n")

    for call in p.calls:
        call.write_handler_decl(f)
        f.write(";\n")

    write_cpp_header_guard_end(f)
    f.close()

def generate_struct_names(file, p):
    """Generate list of structures names.

    Lists the structures used in the IPC protocol, this can be
    used for tools such as pahole.
    """
    f = open(file, "w")
    f.write("ipc_shared_memory\n")
    types = set()
    for call in p.calls:
        for i in call.in_args + call.out_args:
            if i.is_aggregate:
                types.add(i.typename.split(" ")[-1])
    for t in sorted(types):
        f.write(t)
        f.write("\n")
    f.close()

def main():
    """Handle command line and generate a file."""
    parser = argparse.ArgumentParser(description='Protocol generator.')
    parser.add_argument(
        'proto', help='Protocol file to use')
    parser.add_argument(
        'output', type=str, nargs='+',
        help='Output file, uses the name to choose output type')
    args = parser.parse_args()

    p = Proto.load_and_parse(args.proto)

    for output in args.output:
        if output.endswith("ipc_protocol_generated.h"):
            generate_h(output, p)
        if output.endswith("ipc_client_generated.c"):
            generate_client_c(output, p)
        if output.endswith("ipc_client_generated.h"):
            generate_client_h(output, p)
        if output.endswith("ipc_server_generated.c"):
            generate_server_c(output, p)
        if output.endswith("ipc_server_generated.h"):
            generate_server_header(output, p)
        if output.endswith("structs.txt") or output.endswith("ipc-structs.txt"):
            generate_struct_names(output, p)


if __name__ == "__main__":
    main()
