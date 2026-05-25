#ifndef __CUS_CANTP_PORT_H__
#define __CUS_CANTP_PORT_H__


#include <stdint.h>


void Cus_CANTP_Port_EnterCritical( void );


void Cus_CANTP_Port_ExitCritical( void );


uint32_t Cus_CANTP_Port_EnterCritical_FromISR( void );


void Cus_CANTP_Port_ExitCritical_FromISR( uint32_t state );




#endif /* __CUS_CANTP_PORT_H__ */
