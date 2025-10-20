Naming convention: https://www.geeksforgeeks.org/cpp/naming-convention-in-c/

# SERVER

## Install lithium (http server)
from project root: 
`./scripts/setup_lithium `

## Install other dependencies
`sudo apt install cmake g++ libssl-dev libboost-all-dev`

## Make and run server
`cd server`
`make all`
`make run` (test)

`killall server_app` - išjungia serverį

### Testai
`curl "http://localhost:8080/greet?name=Matas"`
`curl -X POST http://localhost:8080/echo -H "X-Custom-Header: secret" -d "hello world"`