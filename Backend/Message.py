from dotenv import load_dotenv
import os
from twilio.rest import Client

load_dotenv()

account_sid = os.environ["TWILIO_ACC_SSID"]
auth_token = os.environ["TWILIO_TOKEN"]
number = os.environ["TWILIO_NUMBER"]
client = Client(account_sid, auth_token)

def fault_device_message(fault_device_message):
    body_message = ""
    
    for device in fault_device_message:
        body_message += f"DeviceID:{device['DeviceId']}\nFaults:{device['Faults']}\nLocation URL:{device['Location']}\nTimeStamp:{device['Timestamp']}\n\n"
    
    
    message = client.messages.create(
    body=body_message,
    from_=number,
    to="+919345333914",
)


    




