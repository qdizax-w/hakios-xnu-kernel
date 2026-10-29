#!/var/jb/bin/sh
# The extensions iOS runs in the background -- widgets above all (chronod renders every widget's timeline and the
# gallery's snapshots), and the others it starts on its own -- at the darwin background policy, every 5 s for any that
# has started since: here nothing draws them on the GPU (no Metal), each one took 70 to 90 % of a core for minutes
# after the boot (29 October: Stocks, Game Center, Wallet, Calendar, Weather, Books, Mail, Photos), and the home
# screen, which SpringBoard and backboardd draw on the CPU beside them, lagged while apps were smooth. In the
# background policy they run at the lowest priority, on what the window server leaves.
while :; do
	/bin/ps -A -o pid=,comm= | /var/jb/usr/bin/grep -E '\.appex/' | while read -r pid rest; do
		/var/jb/usr/sbin/taskpolicy -b -p "$pid" 2>/dev/null
	done
	/var/jb/bin/sleep 5
done
