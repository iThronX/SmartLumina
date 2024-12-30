from typing import List
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field
from motor.motor_asyncio import AsyncIOMotorClient
import uvicorn

app = FastAPI()


MONGO_URI = "mongodb://localhost:27017" 
DATABASE_NAME = "SensorDB"
COLLECTION_NAME = "SensorData"


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
