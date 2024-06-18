/***************************************************************************
 *   Copyright (C) 2015-2018 Microchip Technology Inc.                     *
 *   http://www.microchip.com/support                                      *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/
 
/* Uncomment this for FlashPro JTAG scan logging: */
// #define MICROSEMI_FLASHPRO_DEBUG 1
 
 /*
 * Microsemi FlashPro JTAG driver (via FpcommWrapper API/DLL) for OpenOCD
 * http://www.microsemi.com/products/fpga-soc/design-resources/programming/flashpro
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* For struct jtag_interface */
#include <jtag/interface.h>


#include "microsemi_efp6_client1.h"
#include "microsemi_efp6_client2.h"
#include "microsemi_fpcommwrapper_client.h"

#include "microsemi_api_calls.h"
#include "microsemi_serialize.h"
#include "microsemi_parse.h"

#include "microsemi_socket_client.h"
#include "microsemi_timeout.h"

#include "microsemi_missing_implementation.h"

#include "libbinn/include/binn.h"


fp_implementation_api *efp2 = &efp6implementation2;
fp_implementation_api *fpcw = &fpcommwrapperImplementation;

fp_implementation_api *fpImplementation = &efp6implementation2;


/* Logging status */
static bool g_f_logging = false;

bool fpHandleOk = true;

char fpPartialPort[FP_LONGEST_PORT]="";

// static void print_scan_bits(char *pstr, int nbits, const uint8_t *pscanbits)
// {
//     int i = 0;
//     for (*pstr = '\0'; (i * 8) < nbits; i++)
//     {
//         sprintf((pstr + (i * 2)), "%02x", (NULL == pscanbits) ? 0 : *(pscanbits + i));
//     }
// }


static const Jim_Nvp jim_nvp_boolean_options[] = {
    { .name = "off",      .value = 0  },
    { .name = "on",       .value = 1  },
    { .name = "disable",  .value = 0  },
    { .name = "enable",   .value = 1  },
    { .name = "0",        .value = 0  },
    { .name = "1",        .value = 1  },
    { .name = NULL,       .value = -1 },
};

const char jim_nvp_boolean_options_description[] = "['off'|'on'|'disable'|'enable'|'0'|'1']";

static int microsemi_flashpro_speed(int speed)
{
    if (g_f_logging)
    {
        LOG_INFO("%s(%d)", __FUNCTION__, speed);
    }

    fpImplementation->setTckFrequency(speed);

    return ERROR_OK;
}


static int microsemi_flashpro_speed_div(int speed, int *khz)
{
    if (g_f_logging)
    {
        LOG_INFO("%s(%d)", __FUNCTION__, speed);
    }

    *khz = speed / HZ_PER_KHZ;

    return ERROR_OK;
}


static int microsemi_flashpro_khz(int khz, int *jtag_speed)
{
    if (g_f_logging)
    {
        LOG_INFO("%s(%d)", __FUNCTION__, khz);
    }

    *jtag_speed = khz * HZ_PER_KHZ;
    return ERROR_OK;
}


static int microsemi_fp_execute_scan(struct scan_command *cmd)
{
    if (g_f_logging)
    {
        LOG_INFO("%s is_ir_scan=%d end_state=%d num_fields=%d", __FUNCTION__, cmd->ir_scan, cmd->end_state, cmd->num_fields);
    }

    return fpImplementation->executeScan(cmd);
}


static int microsemi_fp_execute_statemove(struct statemove_command *cmd)
{
    if (g_f_logging)
    {
        LOG_INFO("%s state=%d", __FUNCTION__, cmd->end_state);
    }

    if (!fpHandleOk) return ERROR_OK;

    return fpImplementation->jtagGotoState(cmd);
}


static int microsemi_fp_execute_runtest(struct runtest_command *cmd)
{
    if (g_f_logging)
    {
        LOG_INFO("%s num_cycles=%d and then reaching=%d", __FUNCTION__, cmd->num_cycles, cmd->end_state);
    }

    return fpImplementation->runtest(cmd);
}


static int microsemi_fp_execute_reset(struct reset_command *cmd)
{
    if (g_f_logging)
    {
        LOG_INFO("%s trst=%d srst=%d", __FUNCTION__, cmd->trst, cmd->srst);
    }

    return fpImplementation->setTrst(cmd);
}


static int microsemi_flashpro_execute_command(struct jtag_command *cmd)
{
    // Ordered the JTAG command types in order how likely they will get hit
    // the most frequent ones are on top so less conditions/branching
    // has to be done to invoke the command
    
    if (!fpHandleOk) return ERROR_OK; // quietly ignore commands

    switch (cmd->type) 
    {
        case JTAG_SCAN:
        {
            return microsemi_fp_execute_scan(cmd->cmd.scan);
        }

        case JTAG_RUNTEST:
        {
            return microsemi_fp_execute_runtest(cmd->cmd.runtest);
        }

        case JTAG_RESET:
        {
            return microsemi_fp_execute_reset(cmd->cmd.reset);
        }

        case JTAG_TLR_RESET:
        {
            return microsemi_fp_execute_statemove(cmd->cmd.statemove);
        }

        default:
        {
            LOG_ERROR("Unimplemented JTAG command type encountered: %d", cmd->type);
            LOG_ERROR(MICROSEMI_MISSING_IMPLEMENTATION_STRING);
            break;
        }
    }
    return 0;
}


static int microsemi_flashpro_execute_queue(void)
{
    for (struct jtag_command *cmd = jtag_command_queue; cmd; cmd = cmd->next) 
    {
        if (microsemi_flashpro_execute_command(cmd)) 
        {
            // if any given of commands returned error, ignore the whole queue and return a error
            return ERROR_COMMAND_CLOSE_CONNECTION;
        }
    }
    
    // return OK only if all listed commands executed without issue
    return ERROR_OK;
}


static int microsemi_flashpro_initialize(void)
{
    int error;

    if (g_f_logging)
    {
        LOG_INFO("%s start", __FUNCTION__);
    }

    // printf("Going to try first eFP6a, then eFP6b and then fpcommwrapper\r\n ");

    eFP6_1_warn(); // Display warning if we find some eFP6 rev A devices
    
    // printf("Warning for eFPa displayed if it's detected, now turn for eFPb\r\n");

    // if (strlen(fpPartialPort) > 0) {
    //         printf("Enumerating eFP6b with port %s \r\n", fpPartialPort);
    // } else {
    //     printf("No port selected, enumerating eFP6b without port\r\n");
    // }

    if (efp2->enumerate(fpPartialPort) > 0) 
    {
        // We found some eFP6 (rev 200b)
        fpImplementation = &efp6implementation2;
    }
    else 
    {
        // We didn't found any eFP6 rev B, lets use the fpcommwrapper instead
        fpImplementation = &fpcommwrapperImplementation;

        // printf("We didn't find eFP6 so now the fpcommwrapper turn \r\n");

        if (strlen(fpPartialPort) > 0) {
            // printf("Enumerate the fpcommwrapper now with port %s \r\n", fpPartialPort);
            int portSelectionReturn = fpImplementation->enumerate(fpPartialPort);
            if (portSelectionReturn != ERROR_OK) {
                // printf("Something went wrong in fpcommwrapper enumerate/port selection, abroting\r\n");
                return portSelectionReturn;
            }
        } else {
            // printf("No port selected, so we do not need to do fpcommwrapper enumerate right now \r\n");
        }
    }
    // From this point the fpImplementation pointer will point to some valid implementation.
    // The 'detection' is giving priority to the eFP6, because this way if there
    // is a valid eFP6 device then we do not even have to try starting the fpServer
    // making things easier/simpler

    error = fpImplementation->open();

    if (error == ERROR_OK) fpHandleOk = true;

    return error;
}


static int microsemi_flashpro_quit(void)
{
    int error;
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (!fpHandleOk) return ERROR_OK;

    error = fpImplementation->close(); // Attempt to close the port even when last communication failed
    fpHandleOk = false;
    
    return error;
}


/* ------------------ FlashPro custom OpenOCD commands ------------------------------------*/



COMMAND_HANDLER(handle_microsemi_flashpro_port_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying FlashPro port expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }


    // eFP6
    strcpy(fpPartialPort, CMD_ARGV[0]);

    // printf("Set partial port to %s (not sure yet if eFP6 or fpcommwrapper)\r\n ", fpPartialPort);


    // At this point we do not know which implementation will be used so handle
    // both, for fpServer->fpcommwrapper it needs to be sent through UDP socket
    // for eFP6 just set the global variable here. This should be done in a cleaner
    // way, but will have to do for now. Better it would be to handle it just at
    // the initialize stage and only pass it to the fpServer when eFP6 fails

    // fpServer:
    // binn *request = binn_list();
    // binn *response;
    // microsemi_fp_request delay_type = serialize_set_usb_port(request, CMD_ARGV[0]);
    // if (microsemi_socket_send(request, &response, delay_type)) 
    // {
    //     LOG_ERROR("fpClient, call 'flashpro_port_command' to fpServer expired.");
    //     return ERROR_COMMAND_CLOSE_CONNECTION;
    // }
    // else 
    // {
    //     //printf("Got response back with code %d \n", parse_response_basic(response));
    //     return parse_response_basic(response);
    // }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_binary_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying path to FlashPro server binary (max 512 chars) expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    if (microsemi_socket_set_server_path(CMD_ARGV[0])) 
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_ip_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying IPv4 address expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    if (microsemi_socket_set_ipv4((char *)CMD_ARGV[0])) 
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_port_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single argument specifying port number expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    if (microsemi_socket_set_port(atoi(CMD_ARGV[0]))) 
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_autostart_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying autostart state expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL) 
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    microsemi_server_set_autostart(n->value);

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_fpserver_autokill_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying autokill state expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL) 
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    microsemi_server_set_autokill(n->value);

    return ERROR_OK;
}


COMMAND_HANDLER(handle_microsemi_flashpro_tunnel_jtag_via_ujtag_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying JTAG tunnel state expected expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL) 
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type =  serialize_ujtag_set(request, n->value);

    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'flashpro_tunnel_jtag_via_ujtag' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else 
    {
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
}


COMMAND_HANDLER(handle_microsemi_flashpro_logging_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying logging state expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL)
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    g_f_logging = (n->value == 1);
    return ERROR_OK;

#if 0
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_logging(request, n->value);

    // printf("Delay type for ujtag %d \n", (int)delay_type);

    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient: call 'flashpro_logging' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else 
    {
        command_print(CMD, "microsemi_flashpro logging %s", n->name);
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
#endif

}


COMMAND_HANDLER(handle_microsemi_fpserver_file_logging_command)
{
    if (g_f_logging)
    {
        LOG_INFO("%s", __FUNCTION__);
    }

    if (CMD_ARGC != 1) 
    {
        LOG_ERROR("Single boolean argument specifying logging state expected");
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    const Jim_Nvp* n = Jim_Nvp_name2value_simple(jim_nvp_boolean_options, CMD_ARGV[0]);
    if (n->name == NULL)
    {
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type =  serialize_server_file_logging(request, n->value);

    printf("Delay type for ujtag %d \n", (int)delay_type);

    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient: call 'microsemi_fpserver_file_logging' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else 
    {
        command_print(CMD, "microsemi_fpserver_file_logging %s", n->name);
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }

    return ERROR_OK;
}


static const struct command_registration microsemi_flashpro_exec_command_handlers[] = 
{
    {
        .name =    "port",
        .handler = handle_microsemi_flashpro_port_command,
        .mode =    COMMAND_CONFIG,
        .help =    "identify a specific FlashPro port to be used",
        .usage =   "<flashpro-port-name> e.g. usb71682 (FlashPro3/4/LCPS), S200XTYRZ3 (FlashPro5) etc.",
    },
    {
        .name =    "fpserver_binary",
        .handler = handle_microsemi_fpserver_binary_command,
        .mode =    COMMAND_CONFIG,
        .help =    "path to the fpServer binary",
        .usage =   "<path> defaults to \"fpServer\"",
    },
    {
        .name =    "fpserver_ip",
        .handler = handle_microsemi_fpserver_ip_command,
        .mode =    COMMAND_CONFIG,
        .help =    "IPv4 address to the fpServer, defaults to 127.0.0.1",
        .usage =   "<ip-v4-address>",
    },
    {
        .name =    "fpserver_port",
        .handler = handle_microsemi_fpserver_port_command,
        .mode =    COMMAND_CONFIG,
        .help =    "identify a specific TCP fpserver_port to be used, defaults to 3334",
        .usage =   "<port>",
    },
    {
        .name =    "fpserver_autostart",
        .handler = handle_microsemi_fpserver_autostart_command,
        .mode =    COMMAND_CONFIG,
        .help =    "autostart fpserver with openocd, default off",
        .usage =   jim_nvp_boolean_options_description,
    },
    {
        .name =    "fpserver_autokill",
        .handler = handle_microsemi_fpserver_autokill_command,
        .mode =    COMMAND_CONFIG,
        .help =    "autokill fpserver which is running at the same port, default off",
        .usage =   jim_nvp_boolean_options_description,
    },
    {
        .name =    "fpserver_file_logging",
        .handler = handle_microsemi_fpserver_file_logging_command,
        .mode =    COMMAND_ANY,
        .help =    "control whether fpServer's API and timeouts file logging is on or not",
        .usage =   jim_nvp_boolean_options_description,
    },
    {
        .name =    "tunnel_jtag_via_ujtag",
        .handler = handle_microsemi_flashpro_tunnel_jtag_via_ujtag_command,
        .mode =    COMMAND_ANY,
        .help =    "control whether or not JTAG traffic is \"tunnelled\" via UJTAG",
        .usage =   jim_nvp_boolean_options_description,
    },
    {
        .name =    "logging",
        .handler = handle_microsemi_flashpro_logging_command,
        .mode =    COMMAND_ANY,
        .help =    "control whether or not logging is on",
        .usage =   jim_nvp_boolean_options_description,
    },
    COMMAND_REGISTRATION_DONE
};


static const struct command_registration microsemi_flashpro_command_handlers[] = 
{
    {
        .name =  "microsemi_flashpro",
        .mode =  COMMAND_ANY,
        .help =  "Microsemi FlashPro command group",
        .usage = "",
        .chain = microsemi_flashpro_exec_command_handlers,
    },
    {
        .name =    "microsemi_flashpro_port",
        .handler = handle_microsemi_flashpro_port_command,
        .mode =    COMMAND_CONFIG,
        .help =    "identify a specific FlashPro port to be used",
        .usage =   "<flashpro-port-name> e.g. usb71682 (FlashPro3/4/LCPS), S200XTYRZ3 (FlashPro5) etc.",
    },
    COMMAND_REGISTRATION_DONE
};


struct jtag_interface microsemi_flashpro_interface = 
{
    .name =           "microsemi-flashpro",
    .supported =      0,    /* Don't support DEBUG_CAP_TMS_SEQ */
    .commands =       microsemi_flashpro_command_handlers,
    .transports =     jtag_only,
    .init =           microsemi_flashpro_initialize,
    .quit =           microsemi_flashpro_quit,
    .speed =          microsemi_flashpro_speed,
    .speed_div =      microsemi_flashpro_speed_div,
    .khz =            microsemi_flashpro_khz,
    .execute_queue =  microsemi_flashpro_execute_queue,
};
