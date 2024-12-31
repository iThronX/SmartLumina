from typing import List
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field
from motor.motor_asyncio import AsyncIOMotorClient
import uvicorn
from Message import fault_device_message

app = FastAPI()


MONGO_URI = "mongodb://localhost:27017" 
DATABASE_NAME = "SensorDB"
COLLECTION_NAME = "SensorData"

THRESHOLDS = {
    "Temperature": {"min": 10.0, "max": 50.0},
    "Humidity": {"min": 5.0, "max": 100.0},
    "LDR": {"min": 0, "max": 1023},
    "Voltage": {"min": 3.0, "max": 4.2}
}

client = AsyncIOMotorClient(MONGO_URI)
db = client[DATABASE_NAME]
collection = db[COLLECTION_NAME]


class GPSModel(BaseModel):
    Latitude: float
    Longitude: float


class SensorData(BaseModel):
    DeviceId: str
    Temperature: float
    Humidity: float
    LDR: int
    Voltage: float
    GPS: GPSModel
    Timestamp: str

@app.post("/store-sensor-data/")
async def store_sensor_data(sensor_data: List[SensorData]):
    faulty_devices = []  

    for data in sensor_data:
        
        faults = []
        if not (THRESHOLDS["Temperature"]["min"] <= data.Temperature <= THRESHOLDS["Temperature"]["max"]):
            faults.append(f"Temperature: {data.Temperature}")
        if not (THRESHOLDS["Humidity"]["min"] <= data.Humidity <= THRESHOLDS["Humidity"]["max"]):
            faults.append(f"Humidity: {data.Humidity}")
        if not (THRESHOLDS["LDR"]["min"] <= data.LDR <= THRESHOLDS["LDR"]["max"]):
            faults.append(f"LDR: {data.LDR}")
        if not (THRESHOLDS["Voltage"]["min"] <= data.Voltage <= THRESHOLDS["Voltage"]["max"]):
            faults.append(f"Voltage: {data.Voltage}")

        
        if faults:
            faulty_devices.append({
                "DeviceId": data.DeviceId,
                "Faults": faults,
                "Location": f"https://www.google.com/maps/?q={data.GPS.Latitude},{data.GPS.Longitude}",
                "Timestamp": data.Timestamp
            })
   
    fault_device_message(faulty_devices)

    try:
        
        for d in sensor_data:
            print(d.dict())
        data_to_insert = [data.dict() for data in sensor_data]

        
        result = await collection.insert_many(data_to_insert)
        
        return {
            "message": "All sensor data stored successfully",
            "inserted_ids": [str(id) for id in result.inserted_ids]
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
    
if __name__ == "__main__":
    uvicorn.run(app, host="0.0.0.0", port=8000)
