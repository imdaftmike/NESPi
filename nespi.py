import os
import psutil
import re
import serial
import socket
import subprocess
from subprocess import PIPE, Popen
import threading
import time
from gpiozero import Button, LED


#####################################
# NESPi Cart Reader v0.1 by mike.g  #
#        www.daftmike.com           #
#####################################


#############################################################################################
# Setup the serial port and tell arduino we're ready (allows us to start with a cart already inserted at power-on)

ser = serial.Serial("/dev/ttyS0", 9600, timeout=None)
time.sleep(2)
ser.write("ready")


#############################################################################################
# Get the CPU temerature

def get_cpu_temperature():
    process = Popen(['vcgencmd', 'measure_temp'], stdout=PIPE)
    output, _error = process.communicate()
    return float(output[output.index('=') + 1:output.rindex("'")])


#############################################################################################
# Check the CPU temp every 10 seconds and switch the fan on/off if needed

old_temp = 0
def cpufan():
    fanon = 65 # fan turns ON above this temp
    fanoff = 50 # fan turns OFF below this temp
    global old_temp
    threading.Timer(10.0, cpufan).start()
    cpu_temp = get_cpu_temperature()
    if cpu_temp > fanon and old_temp < fanon:
        print "turning the fan ON ...\n"
        ser.write("fanon")
    if cpu_temp < fanoff and old_temp > fanoff:
        print "turning the fan OFF ...\n"
        ser.write("fanoff")
    old_temp = cpu_temp
cpufan()


#  I want to check the cpu temp at regular intervals and compare it to thresholds
#  to decide if we want to turn the fan on or off.
#  But I only want to send the fan message when the temperature crosses the setpoints.
#  In C on Arduino, I'd detect the 'rising/falling edges' by keeping track of the old value.
#  This code works ok, but I'm not sure how to do it in a Python-way :/

#############################################################################################
# Sends 'message' to port 55355 for RetroArch's network commands

def retroarch_command(message):
    sock = socket.socket(socket.AF_INET,
                         socket.SOCK_DGRAM)
    sock.sendto(message, ("127.0.0.1", 55355))


#############################################################################################
# Kills the task of 'procnames', also forces Kodi to close if it's running

def killtasks(procnames):
    for proc in psutil.process_iter():
        if proc.name() in procnames:
            pid = str(proc.as_dict(attrs=['pid'])['pid'])
            name = proc.as_dict(attrs=['name'])['name']
            print "stopping... " + name + " (pid:" + pid + ")"
            subprocess.call(["sudo", "kill", "-15", pid])

    kodiproc = ["kodi", "kodi.bin"]  # kodi needs SIGKILL -9 to close
    for proc in psutil.process_iter():
        if proc.name() in kodiproc:
            pid = str(proc.as_dict(attrs=['pid'])['pid'])
            name = proc.as_dict(attrs=['name'])['name']
            print "stopping... " + name + " (pid:" + pid + ")"
            subprocess.call(["sudo", "kill", "-9", pid])


#############################################################################################
# Safely shuts-down the Raspberry Pi

def shutdown():
    print "shutdown...\n"
    subprocess.call("sudo shutdown -h now", shell=True)


#############################################################################################
# Returns True if the 'proc_name' process name is currently running

def process_exists(proc_name):
    ps = subprocess.Popen("ps ax -o pid= -o args= ", shell=True, stdout=subprocess.PIPE)
    ps_pid = ps.pid
    output = ps.stdout.read()
    ps.stdout.close()
    ps.wait()
    for line in output.split("\n"):
        res = re.findall("(\d+) (.*)", line)
        if res:
            pid = int(res[0][0])
            if proc_name in res[0][1] and pid != os.getpid() and pid != ps_pid:
                return True
    return False


#############################################################################################
# Check if the console we read from NDEF Record #1 is valid, by checking against a list of supported emulators

def check_console(console):
    emulators = ["amiga", "amstradcpc", "apple2", "arcade", "atari800", "atari2600", "atari5200", "atari7800",
                 "atarilynx", "atarist", "c64", "coco", "dragon32", "dreamcast", "fba", "fds", "gamegear", "gb", "gba",
                 "gbc", "intellivision", "macintosh", "mame-advmame", "mame-libretro", "mame-mame4all", "mastersystem",
                 "megadrive", "msx", "n64", "neogeo", "nes", "ngp", "ngpc", "pc", "ports", "psp", "psx", "scummvm",
                 "sega32x", "segacd", "sg-1000", "snes", "vectrex", "videopac", "wonderswan", "wonderswancolor",
                 "zmachine", "zxspectrum"]
    if console != "":
        if console in emulators:
            print "NDEF Record \"" + console + "\" is a valid system...\n"
            return True

        else:
            print "Could not find \"" + console + "\" in the supported systems list"
            print "Check NDEF Record 1 for a valid system name(all-lowercase)\n"
            ser.write("bad")  # Tell Arduino there was a cart read error
            return False


#############################################################################################
# Return the path of the emulator ready to be used later

def get_emulatorpath(console):
    path = "/opt/retropie/supplementary/runcommand/runcommand.sh 0 _SYS_ " + console + " "
    return path


#############################################################################################
# Check that the rom is valid by looking for the file, tell the cart slot light green if good, red if bad

def check_rom(console, rom):
    # get full rom path and check if it's a file
    romfile = "/home/pi/RetroPie/roms/" + console + "/" + rom
    if os.path.isfile(romfile):
        print "Found \"" + rom + "\"\n"
        ser.write("ok")  # Tell Arduino the cart read was successful

        return True
    else:
        print "But couldn\'t find \"" + romfile + "\""
        print "Check NDEF Record 2 contains a valid filename...\n"
        ser.write("bad")  # Tell Arduino there was a cart read error
        return False


#############################################################################################
# Return the full path of the rom read from NDEF record #2 on the NFC tag

def get_rompath(console, rom):
    # escape the spaces and brackets in rom filename
    rom = rom.replace(" ", "\ ")
    rom = rom.replace("(", "\(")
    rom = rom.replace(")", "\)")
    rom = rom.replace("'", "\\'")

    rompath = "/home/pi/RetroPie/roms/" + console + "/" + rom
    return rompath


#############################################################################################
# If the cartridge is valid when the button is switched on then we can launch the rom

def button_on():
    if cartok:
        procnames = ["retroarch", "ags", "uae4all2", "uae4arm", "capricerpi", "linapple", "hatari", "stella",
                     "atari800", "xroar", "vice", "daphne", "reicast", "pifba", "osmose", "gpsp", "jzintv",
                     "basiliskll", "mame", "advmame", "dgen", "openmsx", "mupen64plus", "gngeo", "dosbox", "ppsspp",
                     "simcoupe", "scummvm", "snes9x", "pisnes", "frotz", "fbzx", "fuse", "gemrb", "cgenesis", "zdoom",
                     "eduke32", "lincity", "love", "alephone", "micropolis", "openbor", "openttd", "opentyrian",
                     "cannonball", "tyrquake", "ioquake3", "residualvm", "xrick", "sdlpop", "uqm", "stratagus",
                     "wolf4sdl", "solarus", "emulationstation", "emulationstatio"]
        killtasks(procnames)
        subprocess.call("sudo openvt -c 1 -s -f " + emulatorpath + rompath + "&", shell=True)
        subprocess.call("sudo chown pi -R /dev/shm", shell=True)  # ES needs permission as 'pi' to access this later
        time.sleep(1)
    else:
        print "no valid cartridge inserted...\n"


#############################################################################################
# Close the emulator when the button is pushed again ("off")

def button_off():
    if process_exists("emulationstation"):
        print "\nemulationstation is running...\n"
        with open('/home/pi/romdetails.txt') as myfile:
            romstring=myfile.readline().replace('/home/pi/RetroPie/roms', 'rom')
            romstring=romstring.replace('\n', '')
            ser.write(romstring)
    else:
        procnames = ["retroarch", "ags", "uae4all2", "uae4arm", "capricerpi", "linapple", "hatari", "stella",
                     "atari800", "xroar", "vice", "daphne", "reicast", "pifba", "osmose", "gpsp", "jzintv",
                     "basiliskll", "mame", "advmame", "dgen", "openmsx", "mupen64plus", "gngeo", "dosbox", "ppsspp",
                     "simcoupe", "scummvm", "snes9x", "pisnes", "frotz", "fbzx", "fuse", "gemrb", "cgenesis", "zdoom",
                     "eduke32", "lincity", "love", "alephone", "micropolis", "openbor", "openttd", "opentyrian",
                     "cannonball", "tyrquake", "ioquake3", "residualvm", "xrick", "sdlpop", "uqm", "stratagus",
                     "wolf4sdl", "solarus"]
        killtasks(procnames)


#  I check if ES is running here because if it *is* then any running game was launched from within ES
#  and we don't want to quit it when the button is pressed. But if the game was launched from a cart
#  then ES will not be running in the background and we *do* want to quit the emulator.

#############################################################################################
# Set BCM 4 HIGH... the arduino reads this to determine if the Raspberry Pi is running

led = LED(4)
led.on()

# If we do a manual shutdown from within ES then our program will be stopped and the pin
# will return to a LOW state, the arduino can read this and cut the power when appropriate

#############################################################################################
# Assign the NES 'power' button to the button functions
onbtn = Button(3)
offbtn = Button(2)
onbtn.when_pressed = button_on
offbtn.when_pressed = button_off


#############################################################################################
# Assume the cart is not valid until we've checked it in the main loop
cartok = False

# Main Loop
while True:
    try:
        line = ser.readline()
        if line != "":
            records = line[:-1].split(', ')  # incoming data looks like: "$$$, $$$, $$$, \n"

            uid = records[0]  # 'uid' is read from the NFC tag, also used for shutdown, reset and cart eject
            console = records[1]  # 'console' is NDEF Record #1
            rom = records[2]  # 'rom' is NDEF Record #2


    except IndexError:
        print "NDEF read error...\n"
        ser.write("bad")  # Tell the Arduino there was a cart read error

#############################################################################################
# Check serial data for a command message in the 1st field
    if uid == "shutdown":
        print "shutdown command received...\n"
        shutdown()

    if uid == "cart_eject":
        print "cart ejected...\n"
        cartok = False

    if uid == "reset":
        print "reset button pressed...\n"
        retroarch_command("RESET")

#############################################################################################
# Check the console and rom data for validity
    if console != "":
        if check_console(console):
            if check_rom(console, rom):
                emulatorpath = get_emulatorpath(console)
                rompath = get_rompath(console, rom)
                cartok = True
