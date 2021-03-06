# Docker container with Version 5.4 of GPDB running

## Build your own container

```
docker build -t pivotaldata/gpdb:5.4 ubuntu-16.04
```

## Run the container

The container will keep running forever until it is stopped and the GreenPlum database 
will be listenning in the port `localhost:5433`

```
docker run -dp 127.0.0.1:5433:5432 pivotaldata/gpdb:5.4
```
_The container takes around 30 seconds to instantiate and start the new GP Instance_

## Stop the database

Execute the following command to get the container id
```
docker ps
```

To Stop the container run the following command replacing `CONTAINER_ID`
with the value retrieved from the previous command executed
```
docker stop CONTAINER_ID
```
