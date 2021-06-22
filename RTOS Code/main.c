
/* Standard includes. */
#include <stdio.h>
#include <conio.h>
#include <stdlib.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

#include "semphr.h"

#define sendFrequency pdMS_TO_TICKS(100) /*This is the data rate of all three simulated sensors*/

/*Enumerated data type to identify what task is sending the data*/
typedef enum
{
	vTaskRandSender1,
	vTaskRandSender2,
	constSender3
} DataSource_t;

/* Define the structure type that will be passed on the sender queue.*/
typedef struct
{
	uint8_t randVal;
	DataSource_t eDataSource;
} Data_t;

/*The data structure to pass on the printing queue used by the periodic printing task*/
typedef struct
{
	uint32_t count;
	uint32_t mean;
	uint32_t movAve;
} Stats_t;

/*Initialise queue and semaphore handles*/
QueueHandle_t senderQueue;				/*The main queue used for generating tasks to send data*/
QueueHandle_t alertQueue;				/*Queue to hold the last 3 values sent by task 2*/
QueueHandle_t statsQueue;				/*Queue to hold a single value of type Stats_t for printing*/
QueueHandle_t movingAveQueue;			/*Queue to hold the last 5 values sent by task 1*/
QueueHandle_t writeQueue;				/*Queue to change value sent by task 3 */

SemaphoreHandle_t qCountingSem;			/*Counting semaphore for Sender queue*/
SemaphoreHandle_t consoleBinarySem;		/*Binary semaphore for console. Taken efore each print statement*/
SemaphoreHandle_t alertqBinarySem;		/*Binary semaphore for alertQueue*/
SemaphoreHandle_t statsqBinarySem;		/*Binary semaphore for statsQueue*/
SemaphoreHandle_t movAveqBinarySem;		/*Binary semaphore for movingAverageQueue*/
SemaphoreHandle_t writeqBinarySem;		/*Binary semaphore for writeQueue*/

/*Periodic task to print statistics of task 1*/
void vTaskPrint(void* pvParameters) {
	vTaskDelay(pdMS_TO_TICKS(5000)); /*Block for first 5 seconds*/
	Stats_t receivedStats;			/*To receive data structure to print*/
	TickType_t xLastWakeTime;		/*To record the start time of when this function is unblocked*/
	for (;;) {
		xLastWakeTime = xTaskGetTickCount();							/*Record time of unblocking*/
		xSemaphoreTake(statsqBinarySem, pdMS_TO_TICKS(10));				/*Take semaphore for queue*/
		xQueueReceive(statsQueue, &receivedStats, pdMS_TO_TICKS(10));	/*Read data from queue*/
		xSemaphoreGive(statsqBinarySem);								/*Give back semaphore for queue*/
		xSemaphoreTake(consoleBinarySem,pdMS_TO_TICKS(10));				/*Take semaphore for console*/
		/*Format and print the recorded statisice of data from task 1*/
		printf("\n\tPeriodic Check\n\tSamples: %d\tMean: %d\tMoving average of 5: %d\n\n",receivedStats.count,receivedStats.mean,receivedStats.movAve);
		xSemaphoreGive(consoleBinarySem);								/*Give back semaphore for console*/

		vTaskDelayUntil(&xLastWakeTime,pdMS_TO_TICKS(5000));			/*Block for 5 seconds from the recorded time of unblocking*/
	}


}

/*Task to calculate and record statistics for task 1*/
void vTaskStats(void* pvParameters) {
	
	static uint32_t count = 0;					/*To record total number of values read*/
	static uint32_t sum = 0;					/*To record the sum of all values read*/
	uint32_t temp = 0;							/*Temporary storage variable*/
	uint32_t movAve = 0;						
	uint32_t t_arr[4] = { 0 };					/*Array to store latest 4 values from queue; to be written back to queue*/
	for (;;) {
		
		if (uxQueueMessagesWaiting(movingAveQueue) == 5) {			/*Queue should be full*/
			movAve = 0;												/*Reset moving average*/
			xSemaphoreTake(movAveqBinarySem, pdMS_TO_TICKS(10));	/*Take semaphore for queue*/
			xQueueReceive(movingAveQueue,&temp,0);					/*Read earliest value from queue*/
			sum += temp;											/*Add to sum and current moving average*/
			movAve += temp;
			count++;												/*Increment count of total values read*/
			for (uint8_t i = 0; i < 4; ++i) {
				xQueueReceive(movingAveQueue, &t_arr[i], 0);		/*Read all remaining values in queue nto array*/
				
				movAve += t_arr[i];									/*Add remaining values current moving average*/
			}
			
			movAve = movAve / 5;									/*Calculate moving average; mean of last 5 values*/
			
			Stats_t newSt = { count , (sum / count) , movAve };		/*Create data structure to write single value on writeQueue*/
			for (uint8_t i = 0; i < 4; ++i) {
				xQueueSendToBack(movingAveQueue, &t_arr[i], 0);		/*Write back the last 4 values to the queue, presrving intial order*/
			}
			xSemaphoreGive(movAveqBinarySem);						/*Give back semaphore for queue*/
			xSemaphoreTake(statsqBinarySem, pdMS_TO_TICKS(10));		/*Take semaphore for queue*/
			xQueueOverwrite(statsQueue, &newSt);					/*Overwrite current value; overwrite is preferred for queues of size 1*/
			xSemaphoreGive(statsqBinarySem);						/*Give back semaphore for queue*/
			
		}
		
	}
	//vTaskDelay(pdMS_TO_TICKS(100));
}
/*Task to print an alert if task 2 sends the same value three times in a row*/
void vTaskAlert(void* pvParameters) {
	/*Variables to store last 3 values as read from queue*/
	uint8_t t1;
	uint8_t t2;
	uint8_t t3;
	for (;;) {
		if (uxQueueMessagesWaiting(alertQueue) == 3) {
			xSemaphoreTake(alertqBinarySem, pdMS_TO_TICKS(10));			/*Take semaphore for queue*/
			xQueueReceive(alertQueue, &t1, 0);							/*Read 2 values from the queue*/
			xQueueReceive(alertQueue, &t2, 0);
			xQueuePeek(alertQueue, &t3, 0);								/*And peek at the third; peeking does not remove value from queue, 
																		but can only read value at front of queue*/
			xQueueSendToFront(alertQueue, &t2,0);						/*Write back the second value read to FRONT of queue. This makes it 
																		so only the earliest value from queue is removed and order is preserved*/
			xSemaphoreGive(alertqBinarySem);							/*Give back semaphore for queue*/
			if (t1 == t2 && t2 == t3) {									/*If all three values are same*/
				xSemaphoreTake(consoleBinarySem, pdMS_TO_TICKS(10));	/*Take semaphore for console*/
				vPrintString("Alert: Task 2 has sent ");				/*Print the alert on the console*/
				printf("%d", t1);
				vPrintString(" three times in a row\n");
				xSemaphoreGive(consoleBinarySem);						/*Give back semaphore for console*/
			}
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	
}
/*Task to change constant value sent by task 3*/
void vTaskWrite(void* pvParameters) {
	uint32_t qval = 0;
	
	for (;;) {							
		if (_kbhit() != 0)											/*Check for key press by user*/
		{
			xSemaphoreTake(consoleBinarySem, 20);					/*Take semaphore for console*/
			/* Remove the key from the input buffer. */
			(void)_getch();
			qval = 0;
			/*Prompt and read new value from console*/
			vPrintString("Enter new value for task 3: ");
			scanf_s(" %d", &qval);
			xSemaphoreGive(consoleBinarySem);						/*Give back semaphore for console*/
			xSemaphoreTake(writeqBinarySem, pdMS_TO_TICKS(10));		/*Take semaphore for queue*/
			xQueueOverwrite(writeQueue, &qval);						/*Overwrite the value in writeQueue that task 3 reads from*/
			xSemaphoreGive(writeqBinarySem);						/*Give back semaphore for queue*/
			

		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
/*Task to send random values*/
void vTaskRandSender(uint8_t* piParameters)				/*An integer array: [ID, MIN, MAX] is passed as parameter*/
{	/*Read parameters into meaningful variables*/
	DataSource_t senderID = piParameters[0];
	uint8_t min = piParameters[1];
	uint8_t max = piParameters[2];
	
	BaseType_t xStatus;
	
	for (;; )
	{
		
		uint8_t val = 0;
		if (max != min)
			val = min + rand() % (max - min);						/*Calculate random value in valid range; tasks 1 and 2*/
		else {
			xSemaphoreTake(writeqBinarySem, pdMS_TO_TICKS(10));		/*Take semaphore for queue*/
			xQueuePeek(writeQueue, &val, 0);						/*Peek at value for task 3: constant sender*/
			xSemaphoreGive(writeqBinarySem);						/*Give back semaphore for queue*/
		}
			
		Data_t structToSend = { val, senderID };					/*Create data structure to write on queue*/
		
		xSemaphoreTake(qCountingSem,100);							/*Take semaphore for queue*/
		xQueueSendToBack(senderQueue, &structToSend, pdMS_TO_TICKS(10));		/*Write data structure to queue*/
		xSemaphoreGive(qCountingSem);								/*Give back semaphore for queue*/
		vTaskDelay(sendFrequency);									/*Block for simulated frequency*/

	}
}

void vTaskController(void* pvParameters)
{
	Data_t xReceivedStruct;
	for (;; )
	{
		xSemaphoreTake(qCountingSem, pdMS_TO_TICKS(10));									/*Take semaphore for queue*/
		xQueueReceive(senderQueue, &xReceivedStruct, sendFrequency);				/*Receive data structure containing value and task ID*/
		xSemaphoreGive(qCountingSem);														/*Give back semaphore for queue*/
		
		if (xReceivedStruct.eDataSource == 1) {		/*If data received form task 1*/
			xSemaphoreTake(consoleBinarySem, pdMS_TO_TICKS(10));							/*Take semaphore for console*/
			vPrintString("Recevied from Task 1:");											/*Print value sent by task 1*/
			printf("% d\n", xReceivedStruct.randVal);
			xSemaphoreGive(consoleBinarySem);												/*Give back semaphore for console*/
			xSemaphoreTake(movAveqBinarySem, pdMS_TO_TICKS(10));							/*Take semaphore for queue*/
			xQueueSendToBack(movingAveQueue, &xReceivedStruct.randVal, pdMS_TO_TICKS(100)); /*Write data from task 1 to movingAverageQueue*/
			xSemaphoreGive(movAveqBinarySem);												/*Give back semaphore for queue*/
		}
		if (xReceivedStruct.eDataSource == 2) {		/*If data from task 2*/
			xSemaphoreTake(consoleBinarySem, pdMS_TO_TICKS(10));							/*Take semaphore for console*/
			vPrintString("Recevied from Task 2: ", xReceivedStruct.randVal);				/*Print value sent by task 2*/
			printf("% d\n", xReceivedStruct.randVal);
			xSemaphoreGive(consoleBinarySem);												/*Give back semaphore for console*/
			xSemaphoreTake(alertqBinarySem, pdMS_TO_TICKS(10));								/*Take semaphore for queue*/
			xQueueSendToBack(alertQueue, &xReceivedStruct.randVal, pdMS_TO_TICKS(100));		/*Write data from task 1 to alertQueue*/
			xSemaphoreGive(alertqBinarySem);												/*Give back semaphore for queue*/
		}
		if (xReceivedStruct.eDataSource == 3) {		/*If data from task 3*/
			xSemaphoreTake(consoleBinarySem, pdMS_TO_TICKS(10));							/*Take semaphore for console*/
			vPrintString("Recevied from Task 3: ", xReceivedStruct.randVal);				/*Print value sent by task 1*/
			printf("% d\n", xReceivedStruct.randVal);
			xSemaphoreGive(consoleBinarySem);												/*Give back semaphore for console*/
		}
		

	}
}


int main(void)
{
	time_t t;
	srand((unsigned)time(&t));								/*Initialise srand for random number generator*/
	/*Initialise semaphores*/
	qCountingSem = xSemaphoreCreateCounting(2,2);
	consoleBinarySem = xSemaphoreCreateBinary();
	alertqBinarySem = xSemaphoreCreateBinary();
	statsqBinarySem = xSemaphoreCreateBinary();
	movAveqBinarySem = xSemaphoreCreateBinary();
	writeqBinarySem = xSemaphoreCreateBinary();
	/*Arrays to send as parameters to vTaskRandSender*/
	uint8_t range1[3] = { 1, 100, 200 };
	uint8_t range2[3] = { 2, 0, 5 };
	uint8_t range3[3] = { 3, 10, 10 };		/*iF min=max, constant value sent by task*/

	uint32_t init = 10;
	/*Initiaise Queues*/
	senderQueue = xQueueCreate(3, sizeof(Data_t));
	alertQueue = xQueueCreate(3, sizeof(uint8_t));
	statsQueue = xQueueCreate(1, sizeof(Stats_t));
	writeQueue = xQueueCreate(1, sizeof(uint32_t));
	movingAveQueue = xQueueCreate(5, sizeof(uint8_t));
	
	/*The initial value for task 3 is 10*/
	xQueueSend(writeQueue,&init,pdMS_TO_TICKS(100));
	/*Three instances created of vTaskRandSender simulating 3 sensors*/
	xTaskCreate(vTaskRandSender, "Rand Sender 1", 200, range1, 4, NULL);
	xTaskCreate(vTaskRandSender, "Rand Sender 2", 200, range2, 4, NULL);
	xTaskCreate(vTaskRandSender, "Const Sender ", 200, range3, 4, NULL);
	/*Create all remaining tasks*/
	xTaskCreate(vTaskController, "Controller", 200, NULL, 3, NULL);
	
	xTaskCreate(vTaskStats, "Task 1", 200, NULL, 2, NULL);
	xTaskCreate(vTaskAlert, "Task 2", 200, NULL, 2, NULL);
	xTaskCreate(vTaskWrite, "Task 3", 200, NULL, 2, NULL);
	xTaskCreate(vTaskPrint, "Task 4", 200, NULL, 2, NULL);
	/*Start the Scheduler*/
	vTaskStartScheduler();

	for (;; );
}