#include "gpio.h"
#include "common.h"
#include "uart.h"
#include "dma.h"

#include "i2c.h"
#include "ov7725.h"

#include "oled_spi.h"

#include "ftm.h"
#include "lptmr.h"


#define offset 77
void turn(int angel){
    angel += offset;
    int pwm = (int)((angel/90.0 + 0.5) * 500);  //90����1.5ms
    printf("turn:%d\r\n", pwm);
    FTM_PWM_ChangeDuty(HW_FTM1, HW_FTM_CH0, pwm);
}

#define DRIVER_PWM_WIDTH 1000
void initDriver(){
    printf("initPWM\r\n");

    for(int i=0;i<0;i++)
        GPIO_QuickInit(HW_GPIOC, i, kGPIO_Mode_OPP);
    PCout(0)=1;
    //ʹ��INH

    FTM_PWM_QuickInit(FTM1_CH0_PA12, kPWM_EdgeAligned, 50);     //����FTM�����ض���ģʽ
    turn(0);
    //��ʼ�����

    FTM_PWM_QuickInit(FTM0_CH0_PC01, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH1_PC02, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH2_PC03, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    FTM_PWM_QuickInit(FTM0_CH3_PC04, kPWM_EdgeAligned, DRIVER_PWM_WIDTH);
    //��ʼ�����PWM���
    
}

void setSpeed(int spd){
    if(spd>0){
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH1, spd);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH0, 0);
        
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH2, spd);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, 0);
    }else
    {
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH1, 0);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH0, -spd);
        
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH2, 0);
        FTM_PWM_ChangeDuty(HW_FTM0, HW_FTM_CH3, -spd);
    }
}



/* �뽫I2C.H�е� I2C_GPIO_SIM ��Ϊ 1 */

// �ı�ͼ���С
//0: 80x60
//1: 160x120
//2: 240x180
#define IMAGE_SIZE  0

#if (IMAGE_SIZE  ==  0)
#define OV7620_W    (80)
#define OV7620_H    (60)

#elif (IMAGE_SIZE == 1)
#define OV7620_W    (160)
#define OV7620_H    (120)

#elif (IMAGE_SIZE == 2)
#define OV7620_W    (240)
#define OV7620_H    (180)

#else
#error "Image Size Not Support!"
#endif

// ͼ���ڴ��
uint8_t gCCD_RAM[(OV7620_H)*((OV7620_W/8)+1)];   //ʹ���ڲ�RAM

/* ��ָ�� */
uint8_t * gpHREF[OV7620_H+1];

/* ���Ŷ��� PCLK VSYNC HREF �ӵ�ͬһ��PORT�� */
#define BOARD_OV7620_PCLK_PORT      HW_GPIOE
#define BOARD_OV7620_PCLK_PIN       (8)
#define BOARD_OV7620_VSYNC_PORT     HW_GPIOE
#define BOARD_OV7620_VSYNC_PIN      (10)
#define BOARD_OV7620_HREF_PORT      HW_GPIOE
#define BOARD_OV7620_HREF_PIN       (9)
/*
����ͷ��������PTA8-PTA15 ֻ������ 0 8 16����ֵ
0 :PTA0-PTA7
8 :PTA8-PTA15
16:PTA16-PTA24
*/
#define BOARD_OV7620_DATA_OFFSET    (0)

static void UserApp(uint32_t vcount);
//����һ֡��������û�����

/* ״̬������ */
typedef enum
{
    TRANSFER_IN_PROCESS, //�����ڴ���
    NEXT_FRAME,          //��һ֡����
}OV7620_Status;

int SCCB_Init(uint32_t I2C_MAP)
{
    int r;
    uint32_t instance;
    instance = I2C_QuickInit(I2C_MAP, 50*1000);
    r = ov7725_probe(instance);
    if(r)
    {
        return 1;
    }
    r = ov7725_set_image_size((ov7725_size)IMAGE_SIZE);
    if(r)
    {
        printf("OV7725 set image error\r\n");
        return 1;
    }
    return 0;
}

//���жϺͳ��ж϶�ʹ��PTE�ж�
void OV_ISR(uint32_t index)
{
    static uint8_t status = TRANSFER_IN_PROCESS;
    static uint32_t h_counter, v_counter;
   // uint32_t i;

    /* ���ж� */
    if(index & (1 << BOARD_OV7620_HREF_PIN))
    {
        DMA_SetDestAddress(HW_DMA_CH2, (uint32_t)gpHREF[h_counter++]);
        //i = DMA_GetMajorLoopCount(HW_DMA_CH2);
        DMA_SetMajorLoopCounter(HW_DMA_CH2, (OV7620_W/8)+1);
        DMA_EnableRequest(HW_DMA_CH2);

        return;
    }
    /* ���ж� */
    if(index & (1 << BOARD_OV7620_VSYNC_PIN))
    {
        GPIO_ITDMAConfig(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_IT_FallingEdge, false);
        GPIO_ITDMAConfig(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_IT_FallingEdge, false);
        switch(status)
        {
            case TRANSFER_IN_PROCESS: //���ܵ�һ֡���ݵ����û�����
                    UserApp(v_counter++);
                    //printf("i:%d %d\r\n", h_counter, i);
                    status = NEXT_FRAME;
                    h_counter = 0;

                break;
            case NEXT_FRAME: //�ȴ��´δ���
                status =  TRANSFER_IN_PROCESS;
                break;
            default:
                break;
        }
        GPIO_ITDMAConfig(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_IT_FallingEdge, true);
        GPIO_ITDMAConfig(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_IT_FallingEdge, true);
        PORTE->ISFR = 0xFFFFFFFF;   //������Ը�PORTE
        h_counter = 0;
        return;
    }
}

void initCamera(){
    uint32_t i;

    printf("OV7725 test\r\n");

    //�������ͷ
    if(SCCB_Init(I2C0_SCL_PB00_SDA_PB01))
    {
        printf("no ov7725device found!\r\n");
        while(1);
    }
    printf("OV7620 setup complete\r\n");

    //ÿ������ָ��
    for(i=0; i<OV7620_H+1; i++)
    {
        gpHREF[i] = (uint8_t*)&gCCD_RAM[i*OV7620_W/8];
    }

    DMA_InitTypeDef DMA_InitStruct1 = {0};

    /* ���ж�  ���ж� �����ж� */
    GPIO_QuickInit(BOARD_OV7620_PCLK_PORT, BOARD_OV7620_PCLK_PIN, kGPIO_Mode_IPD);
    GPIO_QuickInit(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_Mode_IPD);
    GPIO_QuickInit(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_Mode_IPD);

    /* install callback */
    GPIO_CallbackInstall(BOARD_OV7620_VSYNC_PORT, OV_ISR);

    GPIO_ITDMAConfig(BOARD_OV7620_HREF_PORT, BOARD_OV7620_HREF_PIN, kGPIO_IT_FallingEdge, true);
    GPIO_ITDMAConfig(BOARD_OV7620_VSYNC_PORT, BOARD_OV7620_VSYNC_PIN, kGPIO_IT_FallingEdge, true);
    GPIO_ITDMAConfig(BOARD_OV7620_PCLK_PORT, BOARD_OV7620_PCLK_PIN, kGPIO_DMA_RisingEdge, true);

    /* ��ʼ�����ݶ˿� */
    for(i=0;i<8;i++)
    {
        GPIO_QuickInit(HW_GPIOE, BOARD_OV7620_DATA_OFFSET+i, kGPIO_Mode_IFT);
    }

    //DMA����
    DMA_InitStruct1.chl = HW_DMA_CH2;
    DMA_InitStruct1.chlTriggerSource = PORTE_DMAREQ;    //������Ը�PORTE
    DMA_InitStruct1.triggerSourceMode = kDMA_TriggerSource_Normal;
    DMA_InitStruct1.minorLoopByteCnt = 1;
    DMA_InitStruct1.majorLoopCnt = ((OV7620_W/8) +1);

    DMA_InitStruct1.sAddr = (uint32_t)&PTE->PDIR + BOARD_OV7620_DATA_OFFSET/8;  //������Ը�PTE
    DMA_InitStruct1.sLastAddrAdj = 0;
    DMA_InitStruct1.sAddrOffset = 0;
    DMA_InitStruct1.sDataWidth = kDMA_DataWidthBit_8;
    DMA_InitStruct1.sMod = kDMA_ModuloDisable;

    DMA_InitStruct1.dAddr = (uint32_t)gpHREF[0];
    DMA_InitStruct1.dLastAddrAdj = 0;
    DMA_InitStruct1.dAddrOffset = 1;
    DMA_InitStruct1.dDataWidth = kDMA_DataWidthBit_8;
    DMA_InitStruct1.dMod = kDMA_ModuloDisable;

    /* initialize DMA moudle */
    DMA_Init(&DMA_InitStruct1);
}

// IMG
uint8_t gIMG[OV7620_W][OV7620_H];   //ʹ���ڲ�RAM

void findLine(){
    for(int y=0;y<OV7620_H;y++){
        for(int x=0;x<OV7620_W;x++){
            if(gIMG[x][y]==0 && gIMG[x+1][y]){
                x++;continue;
            }
            if(gIMG[x][y]&&gIMG[x+1][y]){
                gIMG[x][y]=0;
            }
        }
    }
}
#define DELTA_MAX 5
int dirsum;
void findCenter(){
    
    int center = 30;
    int lastcenter = 30;
    int delta = 0;
    int left, right;
    int y;
    
    dirsum = 0;
    for(y=OV7620_H-2;y>0;y--){
        for(left = lastcenter;left>0;left--)
            if(gIMG[left][y])break;
        for(right = lastcenter;right<OV7620_W;right++)
            if(gIMG[right][y])break;
        
        center = (left+right)/2;
        delta = center - lastcenter;

        if(y!=OV7620_H-2){
            if(delta>DELTA_MAX | delta<-DELTA_MAX){
                for(int x=0;x<OV7620_W;x++)
                    gIMG[x][y]=1;
                break;
            }else dirsum += delta;
            printf("%d\r\n",delta);
            
        }
        lastcenter = center;
        for(int x=0;x<OV7620_W;x++)
                gIMG[x][y]=0;
        gIMG[center][y]=1;
    }
    
    
    printf("\r\n");
    
}

bool printflag = false;

/* �������һ���� �û������� */
static void UserApp(uint32_t vcount)
{
    for(int y=0;y<OV7620_H-1;y++)
        for(int x=0;x<OV7620_W/8;x++)
            for(int i=0; i<8; i++)
                gIMG[x*8+i][y] = (gpHREF[y][x+1]>>(7-i))%2;
    //��ͼƬ��OV7620_H*OV7620_W/8ӳ�䵽OV7620_H*OV7620_W

    findLine();
    findCenter();
    
    if(printflag){
        printflag = false;
        for(int y=0;y<OV7620_H-1;y++){
             for(int x=0;x<OV7620_W;x++){
                printf("%d",gIMG[x][y]);
            }
            printf("\r\n");
        }
        printf("\r\n");
    }
    
    for(int y=0;y<8;y++){
        LED_WrCmd(0xb0 + y); //0xb0+0~7��ʾҳ0~7
        LED_WrCmd(0x00); //0x00+0~16��ʾ��128�зֳ�16�����ַ��ĳ���еĵڼ���
        LED_WrCmd(0x10); //0x10+0~16��ʾ��128�зֳ�16�����ַ���ڵڼ���
        for(int x=0;x<80;x++){
            uint8_t data = 0;
            for(int i=0;i<8 && y*8+i<OV7620_H ;i++)
                data += gIMG[x][y*8+i]<<(i);
            LED_WrDat(data);
        }
    }
    
    char buf[20] = {0};
    sprintf(buf, "s=%d ", dirsum);
    LED_P8x16Str(80, 0, buf);
    turn(dirsum);
}

//���ڽ����ж�
void UART_RX_ISR(uint16_t byteRec){
    //��ӡ����ͼ��
    printflag = true;

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
    setSpeed(0);
    initOLED();
    initCamera();
    

    while(1)
    {

    }
}
