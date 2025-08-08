# Audentify

Audentify is a music recognition project inspired by Shazam, using a simplifed version of audio fingerprinting to store unique audio digital signatures of songs uploaded to the database for future audio identification. 

## Getting Started

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes. See deployment for notes on how to deploy the project on a live system.

### Prerequisites

Before you run the program, prepare some audiofiles and copy them all to a folder called sample_songs 

```
mkdir -p ./microservices/audiofingerprinting/sample_songs
cp -r /path/to/your/music/* ./microservices/audiofingerprinting/sample_songs/
```

Then copy the three audio decoder files from [dr_libs](https://github.com/mackron/dr_libs) to audentify\microservices\audiofingerprinting\lib\dr_libs:
```
git clone https://github.com/mackron/dr_libs.git audentify\microservices\audiofingerprinting\lib\
```
Also ensure that docker is installed.

### Installing

To start the program, run this in root directory 
```
docker-compose up --build -d
```
And in a web browser open up 
```
localhost:8081
```
If you want to upload more songs into the database you can either run 
```
# Copy music files to the running container
docker cp /path/to/your/music/. audio-fingerprinting:/app/new_songs/

# Register the new songs
docker exec audio-fingerprinting ./audioFingerprintingCLI register /app/new_songs --db /app/data/fingerprints.db
```
or add the songs to the mounted directory
```
# Create the directory
mkdir -p ./music_library

# Add your music files
cp -r /path/to/your/music/* ./music_library/

# Restart the container to see the new mount
docker-compose restart audio-fingerprinting

# Register all songs in the mounted directory
docker exec audio-fingerprinting ./audioFingerprintingCLI register /app/music_library --workers 4
```

## Running the tests

Once you are on the webapp, you can upload links of videos like youtube shorts and tiktoks. 

## Built With

* [Spring Boot]([http://www.dropwizard.io/1.0.2/docs/](https://spring.io/projects/spring-boot) - The web framework used
* [Maven](https://maven.apache.org/) - Dependency Management
* C++ - Used for fingerprinting and audio identification microservice
* Python - Used to scrape audio from video links

## Authors

* **Ray** - *Initial work* - [ruilin808](https://github.com/ruilin808)


## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments

* Inspiration
* Shazam
