# MIT License
# 
# Copyright (c) 2020 Madis Kaal
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import time
import struct
from tkinter import *
from tkinter import filedialog,simpledialog,ttk,messagebox
import sys
import threading
from binascii import unhexlify,hexlify
import serial
from serial.tools import list_ports

class App(Tk):

  def __init__(self,*args,**kwargs):
    Tk.__init__(self, *args, **kwargs)

    self.port=None
    self.find_port()
    self.title("28C256 programmer on %s"%self.serialdevice)    
    self.resizable(width=False, height=True)
    self.columnconfigure(0,weight=1)
    self.rowconfigure(1,weight=1)
    buttonframe=Frame(self,relief=FLAT)
    buttonframe.grid(row=0,column=0,sticky="nw")
    self.worker=None
    self.workercompleted=True
    self.loadbutton=Button(buttonframe,text="Load",command=self.load_file)
    self.savebutton=Button(buttonframe,text="Save",command=self.save_file)
    self.readbutton=Button(buttonframe,text="Read",command=self.read_device)
    self.verifybutton=Button(buttonframe,text="Verify",command=self.verify_device)
    self.writebutton=Button(buttonframe,text="Write",command=self.write_device)
    self.blankbutton=Button(buttonframe,text="Check",command=self.check_device)
    self.erasebutton=Button(buttonframe,text="Erase",command=self.erase_device)
    self.lockbutton=Button(buttonframe,text="Lock",command=self.lock_device)
    self.unlockbutton=Button(buttonframe,text="Unlock",command=self.unlock_device)
    self.loadbutton.grid(row=0,column=0,sticky="nw")
    self.savebutton.grid(row=0,column=1,sticky="nw")
    self.readbutton.grid(row=0,column=2,sticky="nw")
    self.verifybutton.grid(row=0,column=3,sticky="nw")
    self.writebutton.grid(row=0,column=4,sticky="nw")
    self.blankbutton.grid(row=0,column=5,sticky="nw")
    self.erasebutton.grid(row=0,column=6,sticky="nw")
    self.lockbutton.grid(row=0,column=7,sticky="nw")
    self.unlockbutton.grid(row=0,column=8,sticky="nw")
    self.progressbar=ttk.Progressbar(self,orient ="horizontal",length = 100, mode ="determinate")
    self.progressbar.grid(row=0,column=1)
    self.editframe=Frame(self,relief=SUNKEN)
    self.editframe.grid(column=0,row=1,columnspan=2,sticky="nsw")
    self.editframe.columnconfigure(0,weight=1)
    self.editframe.rowconfigure(0,weight=1)
    self.editorwidth=3*16+5+16
    self.buf=bytearray((0xff,)*0x8000)
    self.scrollbar=Scrollbar(self.editframe)
    self.listview=Listbox(self.editframe,width=self.editorwidth,bg=self.cget("bg"),font=("Courier",12),relief=FLAT,yscrollcommand=self.scrollbar.set)
    self.listview.grid(row=0,column=0,sticky="nsw")
    self.scrollbar.grid(column=1,row=0,sticky="nsw")
    self.scrollbar.config(command=self.listview.yview)
    self.listview.bind("<Button-1>", self.on_click)
    self.redraw()

  def load_file(self):
    file=filedialog.askopenfilename()
    if file:
      if file.lower().endswith(".hex"):
        with open(file,"rb") as f:
          hex=f.readlines()
          self.buf=bytearray((0xff,)*0x8000)
          for l in hex:
            l=l.strip()
            if l.startswith(b":"):
              bytes=bytearray(unhexlify(l[1:]))
              cs=0
              for b in bytes:
                cs+=b
              if (cs&255)==0:
                if bytes[3]==0:
                  a=(bytes[1]<<8)|bytes[2]
                  if a+bytes[0]>0x8000:
                     messagebox.showerror(title="File error",message="Address out of bounds")
                     break
                  for i in range(bytes[0]):
                    self.buf[a+i]=bytes[i+4]
                elif bytes[3]==1:
                  break
              else:
                messagebox.showerror(title="Read error",message="Bad checksum")
                break
          self.redraw()
      else:
        with open(file,"rb") as f:
          d=f.read(0x8000)
        self.buf=bytearray(d)
        if len(self.buf)<0x8000:
          self.buf.extend((0xff,)*(0x8000-len(self.buf)))
        self.redraw()

  def hexline(self,a):
    r=bytearray((16,a>>8,a&255,0))
    r.extend(self.buf[a:a+16])
    c=0
    for b in r:
      c+=b
    r.extend((((~c)+1)&255,))
    return ":"+hexlify(r).upper().decode()

  def blankline(self,l):
    l=l[9:-2]
    for c in l:
      if c not in ("f","F"):
        return False
    return True
    
  def save_file(self):
    file=filedialog.asksaveasfilename()
    if file:
      if file.lower().endswith(".hex"):
        with open(file,"wt") as f:
          a=0
          while a<0x8000:
            f.write("%s\n"%self.hexline(a))
            a+=16
          f.write(":00000001FF\n")
      else:
        with open(file,"wb") as f:
          f.write(self.buf)
  
  def start_worker(self,task):
    self.progressbar["maximum"]=0x8000
    self.progressbar["value"]=0
    self.worker=threading.Thread(target=task)
    self.worker.start()
    self.workercompleted=False
    self.after(50,self.check_worker)
  
  def check_worker(self):
    if self.worker.is_alive():
      self.after(50,self.check_worker)
    else:
      self.worker.join()
      self.workercompleted=True
      self.progressbar["value"]=0
      self.redraw()

  def find_port(self):
    self.port=None
    self.serialdevice=None
    ports=list_ports.comports()
    for p in ports:
      if "ft232r" not in p.description.lower() and "0403:6001" not in p.hwid:
        continue
      found=False
      try:
        found=self.open_port(p.device)
      except:
        pass
      self.close_port()
      if found:
        self.serialdevice=p.device
        break
    if self.serialdevice==None:
      messagebox.showerror(title="Error",message="Programmer not found")
      sys.exit(1)
    
  def open_port(self,device=None):
    self.close_port()
    if device==None:
      device=self.serialdevice
    self.port=serial.Serial(device,38400,timeout=0.2)
    if self.port!=None:
      return self.sync_port()
    return self.port!=None
  
  def close_port(self):
    if self.port!=None:
      self.port.close()
      self.port=None
  
  def sync_port(self):
    result=False
    self.port.reset_input_buffer()
    self.port.reset_output_buffer()
    for i in range(3):
      self.send_command("")
      l=self.read_response()
      if l==b">":
        result=True
        break
      self.after(50)
    self.after(50)
    self.port.reset_input_buffer()
    self.port.reset_output_buffer()
    return result
    
  def send_command(self,command):
    self.port.write(b"%s\r\n"%command.strip().encode())
    self.port.flush()

  def lock_device(self):
    self.open_port()
    self.send_command("lock")
    self.read_response()
    self.close_port()
  
  def unlock_device(self):
    self.open_port()
    self.send_command("unlock")
    self.read_response()
    self.close_port()
        
  def read_response(self):
    return self.port.readline().strip()
  
  def read_device(self):
    self.start_worker(self.read_task)

  def read_task(self):
    self.open_port()
    rom=bytearray((0xff,)*0x8000)
    self.send_command("read")
    while True:
      l=self.read_response()
      if l==b'>':
        break
      if l.startswith(b":"):
        bytes=bytearray(unhexlify(l[1:]))
        cs=0
        for b in bytes:
          cs+=b
        if (cs&255)==0:
          if bytes[3]==0:
            a=(bytes[1]<<8)|bytes[2]
            for i in range(bytes[0]):
              rom[a+i]=bytes[i+4]
            self.progressbar["value"]=a
          elif bytes[3]==1:
            self.buf=rom
        else:
          messagebox.showerror(title="Read error",message="Bad checksum")
          break      
    self.close_port()
  
  def check_device(self):
    self.start_worker(self.check_task)
 
  def check_task(self):
    self.open_port()
    self.send_command("blankcheck")
    result=""
    while True:
      l=self.read_response()
      if l==b'>':
        break
      elif l!=b'blankcheck':
        result=result+l.decode()
    if result:
      messagebox.showerror(title="Blank check",message="Blank checking result: %s"%result)
    else:
      messagebox.showinfo(title="Blank check",message="Device is blank (all 0xff)")
    self.close_port()
    
  def verify_device(self):
    self.start_worker(self.verify_task)

  def verify_task(self):
    self.open_port()
    difflist={}
    self.send_command("read")
    while not len(difflist):
      l=self.read_response()
      if l==b'>':
        break
      if l.startswith(b":"):
        bytes=bytearray(unhexlify(l[1:]))
        cs=0
        for b in bytes:
          cs+=b
        if (cs&255)==0:
          if bytes[3]==0:
            a=(bytes[1]<<8)|bytes[2]
            for i in range(bytes[0]):
              if self.buf[a+i]!=bytes[i+4]:
                difflist[a+i]={"adr":a+i,"buf":self.buf[a+i],"device":bytes[i+4]}
            self.progressbar["value"]=a
          elif bytes[3]==1:
            break
        else:
          messagebox.showerror(title="Read error",message="Bad checksum")
          break      
    if len(difflist):
      k=sorted(difflist.keys())[0]
      messagebox.showerror(title="Verification error",
         message="Verification failed at %04x, buf=%02x device=%02x"%(difflist[k]["adr"],difflist[k]["buf"],difflist[k]["device"]))
    else:
      messagebox.showinfo(title="Verification",message="Device and buffer are identical")
    self.close_port()

  def write_device(self):
    self.start_worker(self.write_task)
    
  def write_task(self):
    self.open_port()
    self.send_command("erase");
    while True:
      l=self.read_response()
      if l==b'>':
        break
    self.sync_port()
    a=0
    while a<0x8000:
      l=self.hexline(a)
      if self.blankline(l):
        a+=16
        self.progressbar["value"]=a
      else:
        self.send_command(l)
        while True:
          l=self.read_response()
          #print(l)
          l=l.decode("ascii")
          if l==">":
            a+=16
            self.progressbar["value"]=a
            break
          elif l and l[0] not in (":",">"):
            messagebox.showerror(title="Write error",message="Failed to write : %s"%l)
            a=0x8000
            break
    self.close_port()

  def erase_device(self):
    self.start_worker(self.erase_task)
 
  def erase_task(self):
    self.open_port()
    self.send_command("erase")
    result=""
    t=time.time()+1
    c=0
    while True:
      l=self.read_response()
      if l==b'>':
        break
      elif l!=b'erase':
        result=result+l.decode("ascii")
      if time.time()>t:
        t=time.time()+1
        if c<20:
          c+=1
          self.progressbar["value"]=int((0x8000/20)*c)
    if result:
      messagebox.showerror(title="Erase",message="Erase failed: %s"%result)
    else:
      messagebox.showinfo(title="Erase",message="Device erased (all set to 0xff)")
    self.close_port()
    
  def on_click(self,event):
    row=self.listview.nearest(event.y)
    bbox=self.listview.bbox(row)
    w=bbox[2]-bbox[0]
    cw=w/self.editorwidth
    col=int((event.x-bbox[0])/cw)
    if col>=5 and col<=51:
      adr=int((col-5)/3)+row*16
      answer=simpledialog.askstring("Input","Enter bytes separated by space\nFor example 1 2 0xff 0xfe 'text",parent=self)
      if answer is not None:
        try:
          for b in answer.split():
            if b.startswith("'"):
              for c in b[1:]:
                self.set_value(adr,ord(c))
                adr+=1
            else:
              x=int(b,0)
              self.set_value(adr,x)
              adr+=1
        except:
          messagebox.showerror(title="Input error",message="Invalid input format")
          
  
  def format_row(self,a):
    l="%04x "%a
    t=""
    for x in range(16):
      c=self.buf[a]
      l+="%02x "%(c)
      if c>=32 and c<=126:
        t+=chr(c)
      else:
        t+="."
      a+=1
    return l+t
  
  def redraw(self):
    a=0
    self.listview.delete(0,END)
    while a<len(self.buf):
      self.listview.insert(END,self.format_row(a))
      a+=16
  
  def set_value(self,adr,value):
    if adr<len(self.buf):
      row=adr>>4
      self.buf[adr]=value
      self.listview.delete(row)
      self.listview.insert(row,self.format_row(adr&0xfff0))
      self.listview.see(row)
      self.listview.selection_set(row)

App().mainloop()

