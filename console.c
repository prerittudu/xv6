// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "console.h"

#define UP_ARROW 226
#define DOWN_ARROW 227
#define LEFT_ARROW 228
#define RIGHT_ARROW 229


static void consputc(int);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4

static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory


int back_counter = 0;


static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    
    if(pos > 0) --pos;
  }
  else if(c==LEFT_ARROW)
  {
  	if(pos>0)
  	--pos;
  }
  else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  
  if(c==BACKSPACE)
  crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } 
  else if(c == LEFT_ARROW)
  {
  	uartputc('\b');
  }
  else
    uartputc(c);
  cgaputc(c);
}




#define INPUT_BUF 128
#define MAX_HISTORY 16
struct {
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index 
  uint rightmost;  //the first empty char in line  
} input;


char charstobemovedtemp[INPUT_BUF]; //temporary storage for input.buf in a certain context

//history buffer
struct {
  char bufferArr[MAX_HISTORY][INPUT_BUF]; //holds the actual command strings -
  uint lengthsArr[MAX_HISTORY]; // this will hold the length of each command string
  uint lastCommandIndex;  //the index of the last command entered to history
  int numOfCommmandsInMem; //number of history commands in memory
  int currentHistory;//this will hold the current history view 
} historyBufferArray;


char oldBuf[INPUT_BUF];// details of the command that was written before accessing the history
uint lengthOfOldBuf;
char buf2[INPUT_BUF];


#define C(x)  ((x)-'@')  // Control-x


void charstobemoved()
{
	uint n = input.rightmost-input.r;
	
	for(int i=0;i<n;i++)
	charstobemovedtemp[i]=input.buf[(input.e + i) % INPUT_BUF];
}


void shiftright()
{
	uint n = input.rightmost-input.e;
	
	int i;
	
	for(i=0;i<n;i++)
	{
		char c = charstobemovedtemp[i];
    		input.buf[(input.e + i) % INPUT_BUF] = c;
    		consputc(c);
	}
	
	 memset(charstobemovedtemp, '\0', INPUT_BUF);
  
  	for (i = 0; i < n; i++) 
  	{
    		consputc(LEFT_ARROW);
  	}
}

void shiftleft()
{
	uint n = input.rightmost-input.e;
	
	uint i;
  	consputc(LEFT_ARROW);
  	input.e--;
  	for (i = 0; i < n; i++) 
  	{
   		char c = input.buf[(input.e + i + 1) % INPUT_BUF];
    		input.buf[(input.e + i) % INPUT_BUF] = c;
    		consputc(c);
  	}
  	input.rightmost--;
  	consputc(' ');
  	
  	for (i = 0; i <= n; i++) 
  	{
    		consputc(LEFT_ARROW); 
  	}
}


void eraseCurrentLine()
{
	uint numtoerase = input.rightmost-input.r;
	
	for(int i=0;i<numtoerase;i++)
	{
		consputc(BACKSPACE);
	} 	
}

void chartobemovedtooldbuf()
{
	lengthOfOldBuf = input.rightmost - input.r;
    
    	for (int i = 0; i < lengthOfOldBuf; i++) 
    	{
        	oldBuf[i] = input.buf[(input.r+i)%INPUT_BUF];
    	}	
}

void earaseContentOnInputBuf()
{
  	input.rightmost = input.r;
  	input.e = input.r;
}


void copyBufferToScreen(char * bufToPrint, uint n)
{
	 
	 for (uint i = 0; i < n; i++)
	 {
	 	consputc(bufToPrint[i]);
	 }
}

void copyBufferToInputBuf(char * bufToSaveInInput, uint n){
  
  for (uint i = 0; i < n; i++) {
    input.buf[(input.r+i)%INPUT_BUF] = bufToSaveInInput[i];
  }
  input.e = input.r+n;
  input.rightmost = input.e;
}

void saveCommandInHistory()
{

  historyBufferArray.currentHistory= -1;
  
  if (historyBufferArray.numOfCommmandsInMem < MAX_HISTORY)
    historyBufferArray.numOfCommmandsInMem++;
     
  uint l = input.rightmost-input.r -1;
  
  historyBufferArray.lastCommandIndex = (historyBufferArray.lastCommandIndex - 1)%MAX_HISTORY;
  
  historyBufferArray.lengthsArr[historyBufferArray.lastCommandIndex] = l;
  
  for (uint i = 0; i < l; i++) 
  { 
    	historyBufferArray.bufferArr[historyBufferArray.lastCommandIndex][i] =  input.buf[(input.r+i)%INPUT_BUF];
  }

}


int history(char *buffer, int historyId) 
{
  if (historyId < 0 || historyId > MAX_HISTORY - 1)
    return -2;
  if (historyId >= historyBufferArray.numOfCommmandsInMem )
    return -1;
    
  memset(buffer, '\0', INPUT_BUF);
  
  int tempIndex = (historyBufferArray.lastCommandIndex + historyId) % MAX_HISTORY;
  
  memmove(buffer, historyBufferArray.bufferArr[tempIndex], historyBufferArray.lengthsArr[tempIndex]);
  
  return 0;
}



void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;
  uint tempindex;  

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      doprocdump = 1;
      break;
    case C('U'):  // Kill line.
    
    	if(input.rightmost>input.e)
    	{
    		uint numtoshift = input.rightmost-input.e;
    		uint placetoshift = input.e-input.w;
    		uint i;
    		for (i = 0; i < placetoshift; i++) 
    		{
            		consputc(LEFT_ARROW);
          	}
          	memset(buf2, '\0', INPUT_BUF);
          	for (i = 0; i < numtoshift; i++) 
          	{
            		buf2[i] = input.buf[(input.w + i + placetoshift) % INPUT_BUF];
          	}
          	
          	for (i = 0; i < numtoshift; i++) 
          	{
            		input.buf[(input.w + i) % INPUT_BUF] = buf2[i];
          	}
          
          	input.e -= placetoshift;
          	input.rightmost -= placetoshift;
          	
          	for (i = 0; i < numtoshift; i++)
          	{ 
            		consputc(input.buf[(input.e + i) % INPUT_BUF]);
          	}
          	
          	for (i = 0; i < placetoshift; i++)
          	{
            		consputc(' ');
          	}
          	
          	for (i = 0; i < placetoshift + numtoshift; i++) 
          	{ 
            		consputc(LEFT_ARROW);
		}
	}
	else
	{
      
      		while(input.e != input.w &&
        	    input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        	input.e--;
        	input.rightmost--;
        	consputc(BACKSPACE);
        }
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      
      if (input.rightmost != input.e && input.e != input.w) 
      { 
          shiftleft();
          
      }
      else if(input.e != input.w)
      { 
          input.e--;
          input.rightmost--;
          consputc(BACKSPACE);
      }
      break;
    
    case LEFT_ARROW:
    	if(input.e!=input.w)
    	{
    		input.e--;
    		consputc(c);
	}
	break;
    
    case RIGHT_ARROW:
    	if(input.e<input.rightmost)
    	{
    		consputc(input.buf[input.e % INPUT_BUF]);
          	input.e++;
	}
	else if(input.e == input.rightmost)
	{
		consputc(' ');
          	consputc(LEFT_ARROW);
	}
	
	break;
	
    case UP_ARROW:
    	 if (historyBufferArray.currentHistory < historyBufferArray.numOfCommmandsInMem-1 )
    	 {
          	eraseCurrentLine();
          	
          	if (historyBufferArray.currentHistory == -1)
              		chartobemovedtooldbuf();
              		
		  earaseContentOnInputBuf();
		  historyBufferArray.currentHistory++;
		  tempindex = (historyBufferArray.lastCommandIndex + historyBufferArray.currentHistory) %MAX_HISTORY;
		  
		  copyBufferToScreen(historyBufferArray.bufferArr[tempindex]  , historyBufferArray.lengthsArr[tempindex]);
		  
		  copyBufferToInputBuf(historyBufferArray.bufferArr[tempindex]  , historyBufferArray.lengthsArr[tempindex]);
	}
          
   	break;
    case DOWN_ARROW:
	if(historyBufferArray.currentHistory==0)
	{
		eraseCurrentLine();
            	copyBufferToInputBuf(oldBuf, lengthOfOldBuf);
            	copyBufferToScreen(oldBuf, lengthOfOldBuf);
            	historyBufferArray.currentHistory--;
	}
	else if(historyBufferArray.currentHistory!=-1)
	{
		eraseCurrentLine();
            	historyBufferArray.currentHistory--;
            	tempindex = (historyBufferArray.lastCommandIndex + historyBufferArray.currentHistory)%MAX_HISTORY;
		copyBufferToScreen(historyBufferArray.bufferArr[tempindex]  , historyBufferArray.lengthsArr[tempindex]);
		copyBufferToInputBuf(historyBufferArray.bufferArr[tempindex]  , historyBufferArray.lengthsArr[tempindex]);
	}
	
	break;
	
	case '\r':
          input.e = input.rightmost;
    
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        
        
        c = (c == '\r') ? '\n' : c;
        
        if(input.rightmost>input.e)
        {
        	charstobemoved();
        	input.buf[input.e++ % INPUT_BUF] = c;
            	input.rightmost++;
            	consputc(c);
            	shiftright();
        }
        else 
        {
            	input.buf[input.e++ % INPUT_BUF] = c;
            	input.rightmost = input.e - input.rightmost == 1 ? input.e : input.rightmost;
            	consputc(c);
        }
        
        if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){
        
          saveCommandInHistory();
        
          input.w = input.e;
          
           
          wakeup(&input.r);
          
        }
        
      }
      break;
    }
  }
  release(&cons.lock);
  if(doprocdump) {
    procdump();  // now call procdump() wo. cons.lock held
  }
}


int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.r == input.w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

