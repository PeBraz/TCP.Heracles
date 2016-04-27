from flask import Flask
import sys

app = Flask(__name__)

@app.route("/")
def hello():
	fname = sys.argv[1]
	with open(fname) as f:
		data = f.read()
	
	return data



if __name__ == '__main__':
	app.run(host='0.0.0.0')	