
#ifdef __USE_CMSIS

#include "LPC17xx.h"
#include "lpc17xx_adc.h"
#include "lpc17xx_dac.h"
#include "lpc17xx_gpdma.h"
#include "lpc17xx_timer.h"
#include "lpc17xx_exti.h"
#include "lpc17xx_gpio.h"
#include "lpc17xx_pinsel.h"
#include "lpc17xx_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <cr_section_macros.h>

#endif


const uint32_t tabla_seno[86] = {32704,35072,37440,39808,42112,44416,46592,48704,50752,52736,54528,56256,57856,59328,60672,61824,62848,63680,64384,64896,
    65216,65408,65408,65216,64896,64384,63680,62848,61824,60672,59328,57856,56256,54528,52736,50752,48704,46592,44416,42112,
    39808,37440,35072,32704,30336,27968,25600,23296,20992,18816,16704,14656,12672,10880,9152,7552,
    6080,4736,3584,2560,1728,1024,512,192,0, 0, 192,512,1024,1728,2560,3584,4736,6080,7552,9152,
    10880,12672,14656,16704,18816,20992,23296,25600,27968,30336,
};


#define BUFFERFLASH 0x2007C000
uint16_t *pBF =(uint16_t*)BUFFERFLASH;

volatile static int set_amp = 0 ; // Amplitud normal sin metal
volatile static int amp_actual = 0;
volatile static int amp_relativa = 0; // es la diferencia de amplitud
volatile static int amp_actual_prom=0;  // promedia los ultimos 10 datos de la variable amp_actual
volatile static int EA = 0 ;   //estado de analisis o estado de guardado

static volatile int metal=0;  // =1 significa la presencia de un metal
static volatile int ferro=0;  // =1 significa que es metal ferroso

void configADC(void);
void DMAM_DAC_Config(void);
void DAC_Config(void);
void configEXT0(void);
void configLED(void);
uint16_t buscar_max(void);
void setear_max(void);

void configTIMER0(void){
	
	TIM_TIMERCFG_T tim={0};
	tim.prescaleOpt = TIM_US;
	tim.prescaleValue = 1000;
	TIM_InitTimer(LPC_TIM0,&tim);
	
	TIM_MATCHCFG_T m1={0};
	m1.channel=TIM_MATCH_0;
	m1.resetEn=ENABLE;
	m1.matchValue =1001;
	m1.extOpt=TIM_TOGGLE;
	TIM_ConfigMatch(LPC_TIM0,&m1);
	TIM_PinConfig(TIM_MAT0_0_P1_28);

}

void configADC(void){
	ADC_Init(200000);
	ADC_PinConfig(ADC_CHANNEL_0);
	ADC_StartCmd(1);
	ADC_IntEnable(ADC_CHANNEL_0);
}

void DMAM_DAC_Config(void){

	static GPDMA_LLI_T LLI1 = {0};

	LLI1.srcAddr = (uint32_t)tabla_seno;
	LLI1.dstAddr = (uint32_t)&(LPC_DAC->DACR);
	LLI1.nextLLI = (uint32_t)&LLI1;

	LLI1.control = 86
                 | 1<<19
                 | 1<<22
                 | 1<<26
				 | 1<<31;

	GPDMA_Channel_CFG_T chan = {0};

	chan.channelNum    = GPDMA_CH_7 ;
	chan.transferSize  = 86;
	chan.type          = GPDMA_M2P;
	chan.srcMemAddr    = (uint32_t)tabla_seno;
	chan.dstMemAddr    = 0;
	chan.srcConn       = 0 ;
	chan.dstConn       = GPDMA_DAC ;

	chan.src.width     = GPDMA_WORD ;
	chan.src.burst     = GPDMA_BSIZE_1;
	chan.src.increment = ENABLE;

	chan.dst.width     = GPDMA_WORD ;
	chan.dst.burst     = GPDMA_BSIZE_1;
	chan.dst.increment = DISABLE;

	chan.intTC         = ENABLE;
	chan.intErr        = DISABLE;
	chan.linkedList    = (uint32_t)&LLI1;

	GPDMA_Init();
	GPDMA_SetupChannel(&chan);
	GPDMA_ChannelStart(GPDMA_CH_7);
}

void DAC_Config(void){
	DAC_Init();
	// Como el clock de periferico es de 25M , Para 1 MHz de frecuencia de actualización (1us), el timeout es 25.

	DAC_SetDMATimeOut(25);

	DAC_CONVERTER_CFG_T dacCfg;
	dacCfg.doubleBuffer = DISABLE;
	dacCfg.dmaCounter = ENABLE;    // Habilita el contador de tiempo
	dacCfg.dmaRequest = ENABLE;    // Habilita el disparo hacia el GPDMA
	DAC_ConfigDAConverterControl(&dacCfg);
}

void DMA_IRQHandler(){
	
	GPDMA_ClearIntPending(GPDMA_CLR_INTTC,GPDMA_CH_7);
	
	static int contador=0;
	static uint32_t prom=0;
	
	if(EA == 1){
		amp_actual = buscar_max();
		EA = 0;
		
		if(contador < 10){
			prom += amp_actual;
			contador++;
		
		}else{ // ya estan las 10 muestras 
			prom/=10;
			amp_actual_prom = (int)prom;
			contador=0;
			prom=0;
		}
	}
	
	else{
	// EA == 0 , en modo Obtencion
	ADC_StartCmd(1);
	}

}

void configEXT0(void){
	EXTI_Init();
	EXTI_PinConfig(EXTI_EINT0,EXTI_NOPULL);
	
	EXTI_CFG_T ext={0};
	ext.line=EXTI_EINT0;
	ext.mode=EXTI_EDGE_SENSITIVE;
	ext.polarity=0;
	EXTI_Config(&ext);
	
	EXTI_EnableIRQ(EXTI_EINT0);
}

void EINT0_IRQHandler(){
	EXTI_ClearFlag(EXTI_EINT0);
	setear_max();
}

void configPIN(void){
	PINSEL_CFG_T buz={0};
	buz.pin=28;
	buz.mode=2;
	buz.func=3;
	PINSEL_ConfigPin(&buz);
	GPIO_SetDir(PORT_1,(1<<28),GPIO_OUTPUT);

}

void ADC_IRQHandler(){
	//se termino de convertir el dato
	static int i =0;
	pBF[i] = ADC_ChannelGetData(ADC_CHANNEL_0);
	i++;
	if(i==17){
		i=0;
		EA=1;
	}
	else{
		ADC_StartCmd(1);
	}

}

uint16_t buscar_max(void){
	// busca el maximo y lo devuelve 
	uint16_t valor_max = 0 ;

		for(int i=0; i <= 16; i++){
			 if(pBF[i] > valor_max ){
			 valor_max = pBF[i];
		}
		}
return valor_max;
}

void setear_max(void){
	set_amp =buscar_max();
}

void configUART0(void) {
    // Configurar los pines de transmisión y recepción
    UART_PinConfig(UART_TX0_P0_2);
    UART_PinConfig(UART_RX0_P0_3);

    // Configurar los parámetros de la comunicación a 9600 baudios, 8-N-1
    UART_CFG_T uartCfg = {0};
    uartCfg.baudRate = 9600;
    uartCfg.dataBits = UART_DBITS_8;     // 8 bits de datos
    uartCfg.parity   = UART_PARITY_NONE; // Sin el bit de paridad
    uartCfg.stopBits = UART_STOPBIT_1;   // 1 bit de parada

    //  Inicializar el hardware del UART0 con la estructura definida
    UART_Init(UART0, &uartCfg);

    // Configurar, limpiar las colas FIFO y deshabilitar DMA
    UART_FIFO_CFG_T fifoCfg = {0};
    fifoCfg.resetRxBuf = ENABLE;   // Vacía la cola de recepción
    fifoCfg.resetTxBuf = ENABLE;   // Vacía la cola de transmisión
    fifoCfg.dmaMode    = DISABLE;  // No se usa DMA

    UART_FIFOConfig(UART0, &fifoCfg);

    // Habilitar la transmisión en el registro TER
    UART_TxEnable(UART0);
}


int main(void) {

	configADC();
	DMAM_DAC_Config();
	DAC_Config();
	configEXT0();
	configPIN(); 
	configUART0();
	char tx_buffer[64];

while(1){

	// actualiza el la difrencia de amplitud
	amp_relativa = set_amp - amp_actual_prom;

	switch(amp_relativa){
	
		case -3 ... 3 : // hay amp_relativa debido al ruido 
						metal=0;
						ferro=0;
						break;
			
		case 4 ... 2048:  // hay amp_relativa positiva 
						metal=1;
						ferro=0;
						break;

		case -2048 ... -4:  // hay amp_relativa negativa
						metal=1;
						ferro=1;
						break;

	}
	if(metal==1){
		//activa el partpadeo del led y buzzer 
		TIM_Enable(LPC_TIM0);
		TIM_UpdateMatchValue(LPC_TIM0,TIM_MATCH_0,abs(3000/amp_relativa));

	}else{
		TIM_ResetCounter(LPC_TIM0);
		TIM_Disable(LPC_TIM0);
	}


	int len = sprintf(tx_buffer, "Metal: %d, Ferroso: %d, Amp_relativa: %d\r\n", metal, ferro, amp_relativa);

    // Enviar bloque de datos esperando a que la FIFO se libere
	UART_Send(UART0, (uint8_t *)tx_buffer, len, BLOCKING);
}

    return 0 ;
}
