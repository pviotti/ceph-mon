# Key-value store protocol

## SET

Request:

	S [key] [value]

Response:
 
	A

## GET

Request:

	G [key]

Response:

	[value]

## LIST

Request:

	L

Response:

	[key1]: value1; [key2] value2;...


## REMOVE

Request:

	D [key]

Response:

	A or -1 if the key didn't exist

