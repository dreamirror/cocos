import web
import os
BUF_SIZE = 262144
urls = (
    '/packageUrl/1.png', 'packageUrl',
	'/packageUrl/2.zip', 'packageUrl2',
	'/remoteManifestUrl', 'remoteManifestUrl',
	'/remoteVersionUrl', 'remoteVersionUrl'
)

class index:
    def GET(self):
        return "Hello, world!"

if __name__ == "__main__":
    app = web.application(urls, globals())
    app.run()

class packageUrl:
	def GET(self):
		file_name = '1.png'
		file_path = os.path.join('file', file_name)
		print file_name
		f = None
		try:
			f = open(file_path, "rb")
			web.header('Content-Length',370000)
			web.header('Content-Type','application/octet-stream')
			web.header('Content-disposition', 'attachment; filename=%s' % file_name)
			while True:
				c = f.read(BUF_SIZE)
				if c:
					yield c
				else:
					break
		except Exception, e:
			print e
			yield 'Error'
		finally:
			if f:
				f.close()
				
class packageUrl2:
	def GET(self):
		file_name = '2.zip'
		file_path = os.path.join('file', file_name)
		print file_name
		f = None
		try:
			f = open(file_path, "rb")
			web.header('Content-Length',370000000)
			web.header('Content-Type','application/octet-stream')
			web.header('Content-disposition', 'attachment; filename=%s' % file_name)
			while True:
				c = f.read(BUF_SIZE)
				if c:
					yield c
				else:
					break
		except Exception, e:
			print e
			yield 'Error'
		finally:
			if f:
				f.close()

class remoteManifestUrl:
	def GET(self):
		file_name = 'project.manifest'
		file_path = os.path.join('file', file_name)
		print file_name
		f = None
		try:
			f = open(file_path, "rb")
			web.header('Content-Type','application/octet-stream')
			web.header('Content-disposition', 'attachment; filename=%s' % file_name)
			while True:
				c = f.read(BUF_SIZE)
				if c:
					yield c
				else:
					break
		except Exception, e:
			print e
			yield 'Error'
		finally:
			if f:
				f.close()

class remoteVersionUrl:
	def GET(self):
		file_name = 'version.manifest'
		file_path = os.path.join('file', file_name)
		print file_name
		f = None
		try:
			f = open(file_path, "rb")
			web.header('Content-Type','application/octet-stream')
			web.header('Content-disposition', 'attachment; filename=%s' % file_name)
			while True:
				c = f.read(BUF_SIZE)
				if c:
					yield c
				else:
					break
		except Exception, e:
			print e
			yield 'Error'
		finally:
			if f:
				f.close()
#test

