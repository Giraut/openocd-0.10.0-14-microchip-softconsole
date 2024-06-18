#include <stdio.h>
#include <jtag/interface.h>

#include "microsemi_fpcommwrapper_client.h"

#include "microsemi_api_calls.h"
#include "microsemi_serialize.h"
#include "microsemi_parse.h"

#include "microsemi_socket_client.h"
#include "microsemi_timeout.h"

#include "libbinn/include/binn.h"

// ---------------  Global variables for the programmer ------------------------


fp_implementation_api fpcommwrapperImplementation = {
  .enumerate        = fpcommwrapper_enumerate,
  .open             = fpcommwrapper_open,
  .close            = fpcommwrapper_close,

  .setTckFrequency  = fpcommwrapper_setTckFrequency,
  .readTckFrequency = fpcommwrapper_readTckFrequency,

  .setTrst          = fpcommwrapper_setTrst,
  .jtagGotoState    = fpcommwrapper_jtagGotoState,
  .runtest          = fpcommwrapper_runtest,
  .executeScan      = fpcommwrapper_executeScan
};


// ---------------------------- Programmer - Private ---------------------------

int fpcommwrapper_enumerate(const char* partialPortName)
{
    // instead of enumeration just send the partial port string
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_set_usb_port(request, partialPortName);
    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'flashpro_port_command' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else 
    {
        //printf("Got response back with code %d \n", parse_response_basic(response));
        return parse_response_basic(response);
    }
}


int fpcommwrapper_open(void)
{
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_init_request(request);
    // printf("Delay type for init %d \n", (int)delay_type);
    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'flashpro_initialize' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else 
    {
        return parse_response_basic(response);
    }  
}


int fpcommwrapper_close(void)
{
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type =  serialize_quit_request(request);
    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'flashpro_quit' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
    }
    else 
    {
        //printf("Got response back with code %d \n", parse_response_basic(response));
        microsemi_socket_close();
        return parse_response_basic(response);
    }
}


int fpcommwrapper_setTckFrequency(int32_t tckFreq)
{
    binn *request  = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_speed(request, tckFreq);
    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'speed' to fpServer expired.");
        return ERROR_COMMAND_CLOSE_CONNECTION;
        // return ERROR_FAIL;
    }
    else
    {
        return parse_response_basic(response);
    }  
}


int fpcommwrapper_readTckFrequency(void)
{
  return -1;
}


int fpcommwrapper_setTrst(struct reset_command *cmd)
{
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_reset_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'execute_reset' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code);   

    return ERROR_OK;
}


int fpcommwrapper_jtagGotoState(struct statemove_command *cmd)
{
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_statemove_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type))
    {
        LOG_ERROR("fpClient, call 'execute_statemove' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


int fpcommwrapper_runtest(struct runtest_command *cmd)
{
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_runtest_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type)) 
    {
        LOG_ERROR("fpClient, call 'execute_runtest' to fpServer expired.");
        return 1;
    }
    int response_code = parse_response_basic(response); // will free the response handler
    //printf("Got response back with code %d \n", response_code); 
    return 0;
}


int fpcommwrapper_executeScan(struct scan_command *cmd) 
{
    binn *request = binn_list();
    binn *response;
    microsemi_fp_request delay_type = serialize_scan_command(request, cmd);
    if (microsemi_socket_send(request, &response, delay_type))
    {
        LOG_ERROR("fpClient, call 'execute_scan' to fpServer expired.");
        return 1;
    }
    else {
        mutate_scan_command(response, cmd);
    }
    return 0;  
}