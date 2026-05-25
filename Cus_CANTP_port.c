#include "Cus_CANTP_port.h"



__weak void Cus_CANTP_Port_EnterCritical( void )
{
  __nop();
}


__weak void Cus_CANTP_Port_ExitCritical( void )
{
  __nop();
}


__weak uint32_t Cus_CANTP_Port_EnterCritical_FromISR( void )
{
  __nop();    return 0;
}


__weak void Cus_CANTP_Port_ExitCritical_FromISR( uint32_t state )
{
  (void *)state;  
}

