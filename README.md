# Requirements

- Docker (Install docker and allow regular users to invoke docker without sudo). Example steps how to install Docker for Ubuntu (skip if you have working docker already):

```
sudo apt-get update
sudo apt-get -y install apt-transport-https ca-certificates curl software-properties-common gnupg-utils
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
 
version_name=$(lsb_release -cs)
sudo add-apt-repository  "deb [arch=amd64] https://download.docker.com/linux/ubuntu  ${version_name} stable"
sudo apt-get update
sudo apt-get -y install docker-ce
 
sudo groupadd docker
sudo gpasswd -a ${USER} docker  # if your distro doesn't have ${USER} then just use the currently logged in user
```

Relogging or rebooting sometimes helps to reload the user's group privileges.

Then run the following:

```
sudo service docker restart

sudo systemctl restart docker

docker run hello-world
```

- HDD space: 5.5GB (~5GB for docker container + ~300MB for Work directory)

- First time build (downloads docker container + builds OpenOCD deps + OpenOCD build) takes around ~30mins

- Subsequent build (container and build deps cached) ~10mins

# How to use

- Extract/copy this repository/archive into a `~/Work` folder.

  **Note:** It **really needs** to be the `~/Work` directory, the scripts depend on this location.


- `cd` into that extracted folder and go inside the current openocd version (for example: `cd ~/Work/openocd-0.10.0-14`)


- From that folder invoke `bash ./build.sh`

  **Note:** First time it will take longer as it will downloads the required docker container + build the dependency libraries.
            But then subsequent runs should finish in ~10 mins.


- Finished build artifacts should be found inside the `deploy` folder (for example: `ls -la ~/Work/openocd-0.10.0-14/deploy`)
