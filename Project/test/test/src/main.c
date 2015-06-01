#include "gpio.h"
#include "common.h"
#include "uart.h"
#include "dma.h"
#include "ftm.h"
#include "uart.h"

void turn(int angel){
    int pwm = (int)((angel/180.0 + 1) * 500);  //90����1.5ms��0����1ms��180����2ms
    FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, pwm);
}
#define DRIVER_PWM_WIDTH 5000
void initDriver(){
    printf("initPWM\r\n");
    for(int i=0;i<8;i++)
        GPIO_QuickInit(HW_GPIOC, i, kGPIO_Mode_OPP);

    //��ʼ����������Ϊ���������
    /*
        C0----INH
        C1----OUT1
        C2----OUT2
        C3----OUT3
        C4----OUT4
        C5----LED1
        C6----LED2
        C7----LED3
    */
    //FTM_PWM_QuickInit(FTM0_CH3_PA06, kPWM_EdgeAligned, 50);     //����Ƶ��50HZ��20ms���������ض���ģʽ
    //FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, 750);              //���ó�ʼռ�ձ�750/10000*100% = 7.5% = 1.5ms
    //��ʼ�����

    FTM_PWM_QuickInit(FTM0_CH0_PC01, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH1_PC02, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH2_PC03, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH3_PC04, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    //��ʼ�����PWM���
}


void setLeftSpeed(int spd){
    if(spd>0){
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH1, spd);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH0, 0);
    }else
    {
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH1, 0);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH0, -spd);
    }
}

void setRightSpeed(int spd){
    if(spd>0){
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH2, spd);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, 0);
    }else
    {
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH2, 0);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, -spd);
    }
}

void delay(){
    DelayMs(500);
}

//���ڽ����ж�
void UART_RX_ISR(uint16_t byteRec){
#define spd 1000
    switch(byteRec){
    case 'w':
        setLeftSpeed(spd);
        setRightSpeed(spd);
        break;
    case 's':
        setLeftSpeed(-spd);
        setRightSpeed(-spd);
        break;
    case 'a':
        //turn(60);
        break;
    case 'd':
        //turn(120);
        break;
    default:
        setLeftSpeed(0);
        setRightSpeed(0);
        break;
    }
}

int main(void)
{

    DelayInit();
    /* ��ӡ���ڼ�С�� */

    GPIO_QuickInit(HW_GPIOC, 3, kGPIO_Mode_OPP);
    UART_QuickInit(UART0_RX_PB16_TX_PB17, 115200);

    /* ע���жϻص����� */
    UART_CallbackRxInstall(HW_UART0, UART_RX_ISR);

    /* ����UART Rx�ж� */
    UART_ITDMAConfig(HW_UART0, kUART_IT_Rx, true);

    initDriver();
    PCout(0)=1;

    while(1)
    {

    }
}