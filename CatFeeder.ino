#include <Bridge.h>
#include <Process.h>
#include <YunServer.h>
#include <YunClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>



// PIN CONFIGURATION
// # # # # # # # # # # # # # # #
int powerButtonPin      = 7;      // pin of the power button
int actionButtonPin     = 4;      // pin of the action button
int feederRelayPin      = 12;     // pin of the feeder relay
int lightRelayPin       = 11;     // pin of the button and lcd light relay
int piezoPin            = 8;      // pin of the piezo speaker
int servoPowerPin       = 10;     // pin of the servo power relay
int lockServoPin        = 9;      // pin of the lock servos

// DISPLAY PIN CONFIGURATION
// # # # # # # # # # # # # # # #
int displaySdaPin       = 2;      // this is just for information, it's arduino y�n's default
int displaySclPin       = 3;      // the wire library handles the pins and could not be changed

// TIME CONFIGURATION
// # # # # # # # # # # # # # # #
int displayLogoDuration = 1500;   // time to display the 'cat feeder welcome' message
int powerButtonTimeout  = 500;    // time to push the power button to wake up the device
int actionButtonTimeout = 3000;   // time to push the action button before start feeding
int shutdownTimeout     = 10000;  // time-out to shut down device after last action
int melodyRepeatAmount  = 2;      // amount of times melody should play
int timeAfterMelody     = 2000;   // time between melody and feeding
int feedSmallDuration   = 1250;   // time of motor spinning on a small feeding
int feedNormalDuration  = 2500;   // time of a normal / big feeding the motor have to spin

// SERVO HELPER VARS
// # # # # # # # # # # # # # # #
int servoLockPosition   = 20;      // servo position when box is locked
int servoUnlockPosition = 100;     // servo position when box is unlocked

// RUNTIME HELPER VARS
// # # # # # # # # # # # # # # #
bool isInitialized      = false;  // helps to wait until setup is finished
bool isAwake            = false;  // determine if the system is currently waked up or in standby
bool isLocked           = true;   // determine if the box is actually locked or not
long lastAction         = 0;      // system time of the last user action
int currentDisplay      = 0;      // currently shown display page

// SERVER CONFIGURATION
// # # # # # # # # # # # # # # #
String secret           = "cats"; // secret password to protect tcp connection
int serverPort          = 3120;   // server port to listen on for connections
                                  // this number means 'cat' (c = 3, a = 1, t = 20)


//
// INITIALISATIONS
//
YunServer server(serverPort);
LiquidCrystal_I2C lcd(0x27, 20, 4);
Servo servo;



//
// SETUP AND LOOPER
//



/**
 * arduino setup function
 * automatically called once on every system start-up
 */
void setup()
{
    // setup pins
    pinMode(powerButtonPin,       INPUT);
    pinMode(actionButtonPin,      INPUT);
    pinMode(feederRelayPin,       OUTPUT);
    pinMode(lightRelayPin,        OUTPUT);
    pinMode(piezoPin,             OUTPUT);
    pinMode(servoPowerPin,        OUTPUT);
    pinMode(lockServoPin,         OUTPUT);
    pinMode(displaySdaPin,        OUTPUT);
    pinMode(displaySclPin,        OUTPUT);

    // set pin default states
    digitalWrite(powerButtonPin,  LOW);
    digitalWrite(actionButtonPin, LOW);
    digitalWrite(feederRelayPin,  HIGH);
    digitalWrite(lightRelayPin,   HIGH);
    digitalWrite(servoPowerPin,   HIGH);

    // initialize bridge
    Bridge.begin();

    // initialize tcp server
    server.noListenOnLocalhost();
    server.begin();

    // initialize lcd display
    lcd.init();

    // initialize servo
    servo.attach(lockServoPin);
    servo.write(servoLockPosition);

    // and directly shut down the system
    shutDown();
    isInitialized = true;
}

/**
 * arduino looper function
 * will loop as long the system is alive
 */
void loop()
{
    // skip all if the system is not initialized jet
    if( !isInitialized ) return;

    // the first thing we're handle are connections
    listenToTcp();

    // someone wants to start the whole system
    if( !isAwake && digitalRead(powerButtonPin) == HIGH )
    {
        // wait some time, it might be an error
        delay(powerButtonTimeout);

        // still pushed? ok, time to start everything
        if( digitalRead(powerButtonPin) == HIGH )
            return wakeUp();
    }

    // if the system is awake we can handle some things
    else if( isAwake )
    {
        // if the system was awake for too long shut it down
        if( millis() - shutdownTimeout > lastAction )
            return shutDown();

        // if the power button is pushed we have to do something
        if( digitalRead(powerButtonPin) == HIGH )
        {
            // if even the action button is pushed we should un-/lock the box
            if( digitalRead(actionButtonPin) == HIGH )
            {
                // bring power to the servo
                digitalWrite(servoPowerPin,  LOW);
                delay(100);

                // move servo to new position
                if( isLocked )
                {
                    servo.write(servoUnlockPosition);
                    isLocked = false;
                }
                else
                {
                    servo.write(servoLockPosition);
                    isLocked = true;
                }

                // give' em some time an switch power off
                delay(1000);
                digitalWrite(servoPowerPin,  HIGH);

                // wait until buttons being released once
                while( digitalRead(powerButtonPin) == HIGH && digitalRead(actionButtonPin) == HIGH );

                return;
            }

            // otherwise only change the display
            else
            {
                currentDisplay++;
                lastAction = millis();
                showSystemDisplay();

                // wait until the button is released one time
                while( digitalRead(powerButtonPin) == HIGH );
                return;
            }
        }

        // if the action button is pushed on feed screens
        if( digitalRead(actionButtonPin) == HIGH && (currentDisplay == 2 || currentDisplay == 3) )
        {
            delay(actionButtonTimeout);

            // button is still pushed after delay? start to feed the cats ...
            if( digitalRead(actionButtonPin) == HIGH )
            {
                if( currentDisplay == 2 )
                    feedTheCats("normal", "device");
                else if( currentDisplay == 3 )
                    feedTheCats("small", "device");
            }
        }
    }
}



//
// OWN FUNCTION DEFINITIONS
//



/**
 * get everything the user would need started
 * this should be called after a little power-button-push time-out
 */
void wakeUp()
{
    digitalWrite(lightRelayPin, LOW);

    // get display to work
    lcd.clear();
    lcd.backlight();
    lcd.display();

    // write a little welcome message, just to be fine
    lcd.setCursor(0,0);
    lcd.print("+------------------+");
    lcd.setCursor(0,1);
    lcd.print("|    Cat Feeder    |");
    lcd.setCursor(0,3);
    lcd.print("+------------------+");

    // everybody loves a little animation :)
    lcd.setCursor(0,2);
    lcd.print("|                  |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("|-                 |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("|w -               |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("|eow -             |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("| meow -           |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("| - meow -         |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("|   - meow -       |");
    delay(100);
    lcd.setCursor(0,2);
    lcd.print("|     - meow -     |");
    delay(100);

    // wait some time to recognize the message
    delay(displayLogoDuration);

    // get some information for all functions
    isAwake = true;
    lastAction = millis();

    // now show the real system screen
    showSystemDisplay();
}

/**
 * put the device and all unnecessary components to sleep
 * this should be triggered when the wake time-out is reached
 */
void shutDown()
{
    // turn button lights off
    digitalWrite(lightRelayPin, HIGH);

    // turn display off
    lcd.clear();
    lcd.noBacklight();
    lcd.noDisplay();

    // don't forget to enable the sleep mode
    isAwake = false;
    lastAction = 0;
    currentDisplay = 0;
}

/**
 * listen to tcp connections
 * known what to do if we're receive some data
 */
void listenToTcp()
{
    // get incoming clients from server
    YunClient client = server.accept();

    // is a client available?
    if( client )
    {
        client.setTimeout(5);

        String cmd = client.readString();
        cmd.trim();

        // select action by received command
             if( cmd == secret + "_feed_small" )  client.print(automaticFeed("small", "command"));
        else if( cmd == secret + "_feed_normal" ) client.print(automaticFeed("normal", "command"));
        else if( cmd == secret + "_cron_small" )  client.print(automaticFeed("small", "cronjob"));
        else if( cmd == secret + "_cron_normal" ) client.print(automaticFeed("normal", "cronjob"));
        else if( cmd == secret + "_system_info" ) client.print("{\"l\":\"" + executeScript("getLast") + "\","+
                                                                "\"n\":\"" + executeScript("getNext") + "\","+
                                                                "\"t\":\"" + executeScript("getCount") + "\","+
                                                                "\"u\":\"" + millis() + "\"}");
        else                                      client.print("meow");

        // we're done, stop client
        client.stop();
    }
}

/**
 * the almighty function !
 * finally feed the beasts
 */
void feedTheCats(String amount, String starter)
{
    // get the cats noticed
    playMelody(melodyRepeatAmount);

    // start dispenser motor
    digitalWrite(feederRelayPin,  LOW);

    // wait for configured time
    if( amount == "normal" )
        delay(feedNormalDuration);
    else
        delay(feedSmallDuration);

    // and stop motor afterwards
    digitalWrite(feederRelayPin,  HIGH);

    // increase feed amount and log date
    executeScript("updateFeedLogs", starter);

    lastAction = millis();
}

/**
 * automatically feeding method
 * called whenever the feed is started by the system or network
 */
int automaticFeed(String amount, String starter)
{
    // wake system
    wakeUp();

    // feed the cats and wait a short time after
    feedTheCats(amount, starter);
    delay(1000);

    // and go back to sleep after feeding
    shutDown();

    return 1;
}

/**
 * execute a php action script on sd card
 * and return the result as string
 * 
 * possible scripts:
 * - getCount
 * - getLast
 * - getNext
 * - incrementCount
 * - updateLast
 */
String executeScript(String script)
{
    return executeScript(script, "");
}

/**
 * execute a php action script with parameter on sd card
 * and return the result as string
 * 
 * possible scripts:
 * - appendFeed
 * - updateFeedLogs
 */
String executeScript(String script, String param)
{
    Process p;
    p.begin("/usr/bin/php-cli");
    p.addParameter("/mnt/sda1/arduino/www/actions/" + script + ".php");

    if( param != "" )
        p.addParameter("-" + param);

    p.run();

    String result = "";

    while( p.available() > 0 )
        result = result + (char)p.read();

    return result;
}

/**
 * print different informations onto the display
 * the global variable 'currentDisplay' represents the current screen to show
 */
void showSystemDisplay()
{
    lcd.clear();

    // loop screens
    if( currentDisplay > 3 ) currentDisplay = 0;

    // display system info screen
    if( currentDisplay == 1 )
    {
        long timeDiff = millis() / 1000;

        // get days
        int days = floor(timeDiff/86400);
        timeDiff = timeDiff-days*86400;

        // get hours
        int hours = floor(timeDiff/3600);
        timeDiff = timeDiff-hours*3600;

        // get minutes
        int minutes = floor(timeDiff/60);
        timeDiff = timeDiff-minutes*60;

        // get rest as seconds
        int seconds = timeDiff;

        lcd.setCursor(0,0);
        lcd.print("System Uptime:");
        lcd.setCursor(0,1);
        lcd.print(days + String("D ") + hours + "H " + minutes + "M " + seconds + "S");
        lcd.setCursor(0,2);
        lcd.print("Feeded:");
        lcd.setCursor(0,3);
        lcd.print(executeScript("getCount") + " Times");
    }

    // display screen to start feeding's
    else if( currentDisplay == 2 || currentDisplay == 3 )
    {
        String label = currentDisplay == 2 ? "NORMAL" : "SMALL";

        lcd.setCursor(0,1);
        lcd.print("Start " +  label + " feeding");
        lcd.setCursor(0,2);
        lcd.print("(hold action button)");
    }

    // display screen for last and next feeding's
    else
    {
        lcd.setCursor(0,0);
        lcd.print("Last Feed:");
        lcd.setCursor(0,1);
        lcd.print(executeScript("getLast"));
        lcd.setCursor(0,2);
        lcd.print("Next Feed:");
        lcd.setCursor(0,3);
        lcd.print(executeScript("getNext"));
    }
}

/**
 * play a melody the cats will truly notice :)
 * so save space we don't use definitions for that
 */
void playMelody(int times)
{
    // the melody tones array
    int melody[] = { 4699, 4699, 3520, 4699 };

    // duration of the single tones, means 1/6
    int duration = 6;

    for( int t = 0; t < times; t++ )
    {
        for( int i = 0; i < 4; i++ )
        {
            // pass tone to selected pin
            tone(piezoPin, melody[i], 1000/duration);

            // get a bit of time between the tones
            delay(1000 / duration * 1.30 + 80);

            // and don't forget to switch of the tone afterwards
            noTone(piezoPin);
        }

        // wait some time after melody has played
        lastAction = millis();
        delay(timeAfterMelody);
    }
}
