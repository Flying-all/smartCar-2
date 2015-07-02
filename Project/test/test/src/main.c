#include "gpio.h"
#include "common.h"
#include "uart.h"
#include "dma.h"

#include "i2c.h"
#include "ov7725.h"

#include "oled_spi.h"

#include "ftm.h"
#include "lptmr.h"
#include "pit.h"


#define offset 77
void turn(float angel){
    angel += offset;
    int pwm = (int)((angel/90.0 + 0.5) * 500);  //90����1.5ms
    //printf("turn:%d\r\n", pwm);
    FTM_PWM_ChangeDuty(HW_FTM1, HW_FTM_CH0, pwm);
}

#define DRIVER_PWM_WIDTH 1000
void setSpeed(int spd);
void initDriver(){
    printf("initPWM\r\n");

    //for(int i=0;i<0;i++)
    GPIO_QuickInit(HW_GPIOC, 0, kGPIO_Mode_OPP);
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
    setSpeed(0);
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

/* �뽫I2C.H�е� I2C_GPIO_SIM ��Ϊ 1 */

// �ı�ͼ���С
//0: 80x60
//1: 160x120
//2: 240x180
#define IMAGE_SIZE  1

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

#define WIDTH OV7620_W
#define HEIGHT OV7620_H-1

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


bool printflag = false;
//���ڽ����ж�
void UART_RX_ISR(uint16_t byteRec){
    //��ӡ����ͼ��
    if(byteRec == ' ')printflag = true;

}
// IMG
uint8_t IMG[OV7620_W][OV7620_H];   //ʹ���ڲ�RAM


int white[HEIGHT];
int whiteF[HEIGHT];
bool crossflag = false;
void findType(){
    int y;
    int x;
    
    int lastwhite = 0;
    for(y=0;y<HEIGHT;y++){
        int whitedots=0;
        for(x=0;x<WIDTH;x++){
            if(IMG[x][y]==0)whitedots++;
        }
        white[y] = whitedots;
        whiteF[y] = whitedots - lastwhite;
        lastwhite = whitedots;
    }
    //ͳ�ư׵�����ͽ���΢��
    
    //ֱ������ת�䣬��ת���΢������Ч������0~2��ͨ����1���ң�һ��С��0�ͱ�ʾ�������ĸ��Ŵ���
    //ʮ��·��΢������Ч��������һ�δ���0��һ��С��0��������ֵҲ��0~2
    
    int crossstart = 0;
    int crossend = 0;
    for(y=HEIGHT;y>0;y--){
        if(whiteF[y]<3&&whiteF[y]>-3)continue;  //����ģʽ
        if(whiteF[y]<-5){       //ʮ��·�ڿ�ʼ
            crossstart = y;
            while(whiteF[y]<5 && y)y--;//�ߵ�ʮ��·��β��
            while(whiteF[y]>2 && y)y--;//ʮ��·�ڽ���
            if(y<=0)break;
            crossend = y;
            break;
        }
    }
    
    if( (crossstart-crossend>15) && crossend && white[(crossstart+crossend)/2] > 120){
        crossflag = true;
        //��ʮ����ʼλ�ô����������������߽�
        crossstart+=5;
        crossend-=5;
        //printf("crossstart:%d,crossend:%d\r\n", crossstart, crossend);
        int left1 = WIDTH/2;
        int right1 = WIDTH/2;
        while(left1){
            if(IMG[left1][crossstart] == 1 && (IMG[left1+1][crossstart] == 0))break;
            left1--;
        }
        while(right1 < WIDTH-1){
            if(IMG[right1][crossstart] == 0 && (IMG[right1+1][crossstart] == 1))break;
            right1++;
        }
        IMG[left1][crossstart] = 1;
        IMG[right1][crossstart] = 1;


        //��ʮ����ֹλ�ô����������������߽�
        int left2;
        int right2;
        float k1 = 0;
        float k2 = 0;
        
        while(crossend){
            left2 = WIDTH/2;
            right2 = WIDTH/2;
            while(left2){
                if(IMG[left2][crossend] == 1 && (IMG[left2+1][crossend] == 0))break;
                left2--;
            }
            while(right2 < WIDTH-1){
                if(IMG[right2][crossend] == 0 && (IMG[right2+1][crossend] == 1))break;
                right2++;
            }
            IMG[left2][crossend] = 2;
            IMG[right2][crossend] = 2;

            k1 = (float)(left2-left1)/(crossend-crossstart);
            k2 = (float)(right2-right1)/(crossend-crossstart);
            if(k1<0 && k2>0)break;
            crossend--;
        }
        
        if(crossend){
            //���߲���
            for(int i=0;i<(crossstart-crossend) && crossend+i < HEIGHT-1;i++){
                IMG[(int)(left2+k1*i)][crossend+i] = 2;
                IMG[(int)(right2+k2*i)][crossend+i] = 2;
            }
        }
    }else crossflag = false;
}

void findCenter();

#define DELTA_MAX 3
int average;

/* �������һ���� �û������� */
static void UserApp(uint32_t vcount)
{
    for(int y=0;y<OV7620_H-1;y++)
        for(int x=0;x<OV7620_W/8;x++)
            for(int i=0; i<8; i++)
                IMG[x*8+i][y] = (gpHREF[y][x+1]>>(7-i))%2;
    //��ͼƬ��OV7620_H*OV7620_W/8ӳ�䵽OV7620_H*OV7620_W

    findType();
    findCenter();
    
    if(printflag){
        printflag = false;
        //��ӡ��ͼ��
        printf("start\r\n");
        for(int y=0;y<OV7620_H-1;y++){
            for(int x=0;x<OV7620_W;x++){
                printf("%d", IMG[x][y]);
            }
            printf("\r\n");
        }
        
    }
    
    
    //��ӡ����Ļ��
    for(int y=0;y<8;y++){
        LED_WrCmd(0xb0 + y); //0xb0+0~7��ʾҳ0~7
        LED_WrCmd(0x00); //0x00+0~16��ʾ��128�зֳ�16�����ַ��ĳ���еĵڼ���
        LED_WrCmd(0x10); //0x10+0~16��ʾ��128�зֳ�16�����ַ���ڵڼ���
        for(int x=0;x<80;x++){
            uint8_t data = 0;
            for(int i=0;i<8 && (y*8+i)*2<HEIGHT ;i++){
                data += (IMG[x*2][(y*8+i)*2] > 0 | IMG[x*2+1][(y*8+i)*2] > 0)<<(i);
            }
            LED_WrDat(data);
        }
    }
    
    if(crossflag)LED_P8x16Str(80, 0, "cross ");
    else LED_P8x16Str(80, 0, "normal");

    
}

int centers[HEIGHT] = {0};

int err2 = 0;
void findCenter(){
    int y=HEIGHT;

    int center = WIDTH/2;       //��������
    int s = 0;          //�����ۻ����
    int sum = 0;        //������
    
    int left;
    int right;
    
    int err = 0;
    
    while(y){
        left = center-1;
        right = center+1;
        
        while(left){
            if(IMG[left][y])break;
            left--;
        }
        while(right < WIDTH){
            if(IMG[right][y])break;
            right++;
        }
        
        center = (left+right)/2;
        centers[sum] = center;
        if(right-left<10){
            err++;
            break;
        }
        if(err>12)break;
        s += center;
        sum ++;
        IMG[center][y] = 1;
        y--;
    }
    
    char buf[20]={0};
    
    if(sum>10){
        average = s/sum;
        average -= 80;
        LED_P8x16Str(80, 3, buf);
        turn(average/1.5);
        setSpeed(1800+(sum-60)*20+(25-abs(average))*20);
        err2=0;
    }else{
        err2++;
        if(err2>100){
            setSpeed(0);
            turn(0);
            while(1);
        }
    }

    sprintf(buf, "a=%d ", average);
    LED_P8x16Str(80, 1, buf);
    sprintf(buf, "h=%d ", sum);
    LED_P8x16Str(80, 2, buf);
}

float differ = 0;

void setSpeed(int spd){
#define kchasu 60
    if(average>0){
        setLeftSpeed(spd+spd*average/kchasu);
        setRightSpeed(spd);
    }else{
        //-spd*abs(average)*0.32/30
        setLeftSpeed(spd);
        setRightSpeed(spd-spd*average/kchasu);
    }
}

void PIT_ISR(void)
{
    int value; /* ��¼����������� */
    uint8_t dir; /* ��¼��������ת����1 */
    /* ��ȡ������������ */
    FTM_QD_GetData(HW_FTM2, &value, &dir);
    printf("value:%6d dir:%d  \r", value, dir);
    //FTM_QD_ClearCount(HW_FTM2); /* �����Ƶ������Ҫ��ʱ���Countֵ  */
}

int main(void)
{
    DelayInit();
    /* ��ӡ���ڼ�С�� */
    
    GPIO_QuickInit(HW_GPIOD, 10, kGPIO_Mode_OPP);
    UART_QuickInit(UART0_RX_PB16_TX_PB17, 115200);
    /* ע���жϻص����� */
    UART_CallbackRxInstall(HW_UART0, UART_RX_ISR);

    /* ����UART Rx�ж� */
    UART_ITDMAConfig(HW_UART0, kUART_IT_Rx, true);

    initOLED();
    initDriver();
    initCamera();
    
    //�û����Ʋ���
    GPIO_QuickInit(HW_GPIOC, 8, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 10, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 12, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 14, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 16, kGPIO_Mode_IPU);
    GPIO_QuickInit(HW_GPIOC, 18, kGPIO_Mode_IPU);
#define differadd 0.02
    if(PCin(8))differ += differadd;
    if(PCin(10))differ += differadd;
    if(PCin(12))differ += differadd;
    if(PCin(14))differ += differadd;
    if(PCin(16))differ += differadd;
    if(PCin(18))differ += differadd;
    
    printf("differ=%f\r\n", differ);
    
    //��������ʼ��
    FTM_QD_QuickInit(FTM2_QD_PHA_PA10_PHB_PA11, kFTM_QD_NormalPolarity, kQD_PHABEncoding);
    
    /* ����PIT�ж� */
//    PIT_QuickInit(HW_PIT_CH0, 1000*10);
//    PIT_CallbackInstall(HW_PIT_CH0, PIT_ISR);
//    PIT_ITDMAConfig(HW_PIT_CH0, kPIT_IT_TOF, true);
    
    while(1)
    {
        //setSpeed(3000);
        //turn(30);
        //DelayMs(1000);
        //turn(0);
        DelayMs(1000);
        PDout(10) = !PDout(10);
        
    }
}
