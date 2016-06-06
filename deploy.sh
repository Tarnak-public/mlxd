#! /bin/sh

# define hosts to deploy to

case $1 in   
    -pi)
        user="pi"
        hosts=("192.168.72.21")
        serviceSetupCommand="sudo update-rc.d mlxd defaults"
        sudoCmd="sudo "
        monitDir="/etc/monit/conf.d/"
        ;;
    *)
        echo "please run with $0 -pi"
        exit 1
esac

for i in ${hosts[@]}; do
    ssh ${user}@${i} "cd /var/services/mlxd; git pull";
    ssh ${user}@${i} "cd /var/services/mlxd; make"
    ssh ${user}@${i} "${sudoCmd}cp -f /var/services/mlxd/init.d/mlxd /etc/init.d/"
    #ssh ${user}@${i} ${serviceSetupCommand}
    #ssh ${user}@${i} "${sudoCmd}/etc/init.d/mlxd stop"
    ssh ${user}@${i} "${sudoCmd}rm -f /var/services/mlxd/app.pid"
    ssh ${user}@${i} "${sudoCmd}cp -f /var/services/mlxd/monit.d/mlxd ${monitDir}"
    #ssh ${user}@${i} "${sudoCmd}service monit reload"
done
