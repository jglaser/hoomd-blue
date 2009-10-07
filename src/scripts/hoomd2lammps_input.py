#! /usr/bin/python

# This script converts a hoomd xml file to a LAMMPS input data file
import xml.dom.minidom
import sys

if len(sys.argv) != 3:
	print "Usage: hoomd2lammps_input.py filname.xml output.data"
	sys.exit(1);
	
dom = xml.dom.minidom.parse(sys.argv[1]);

# start by parsing the file
hoomd_xml = dom.getElementsByTagName('hoomd_xml')[0];
configuration = hoomd_xml.getElementsByTagName('configuration')[0];

# read the box size
box 	= configuration.getElementsByTagName('box')[0];
Lx = box.getAttribute('lx');
Ly = box.getAttribute('ly');
Lz = box.getAttribute('lz');

# parse the particle coordinates
position = configuration.getElementsByTagName('position')[0];
position_text = position.childNodes[0].data
xyz = position_text.split()
print "Found", len(xyz)/3, " particles";

# parse the velocities
velocity_nodes = configuration.getElementsByTagName('velocity')
velocity_xyz = [];
if len(velocity_nodes) == 1:
	velocity = velocity_nodes[0];
	velocity_text = velocity.childNodes[0].data
	velocity_xyz = velocity_text.split()
	if len(velocity_xyz) != len(xyz):
		print "Error Number of velocities doesn't match the number of positions"
		sys.exit(1);
	print "Found", len(velocity_xyz)/3, " velocities";

# parse the particle types
type_nodes = configuration.getElementsByTagName('type');
if len(type_nodes) == 1:
	type_text = type_nodes[0].childNodes[0].data;
	type_names = type_text.split();
	if len(type_names) != len(xyz)/3:
		print "Error! Number of types differes from the number of particles"
		sys.exit(1);
else:
	print "Error! The type node must be in the xml file"
	sys.exit(1);

# convert type names to type ids
type_id = [];
type_id_mapping = {};
for name in type_names:
	name = name.encode();
	# use the exising mapping if we have made one
	if name in type_id_mapping:
		type_id.append(type_id_mapping[name]);
	else:
		# otherwise, we need to create a new mapping
		type_id_mapping[name] = len(type_id_mapping)+1;
		type_id.append(type_id_mapping[name]);

print "Mapped particle types:"
print type_id_mapping

# parse the bonds
bond_nodes = configuration.getElementsByTagName('bond')
bond_a = [];
bond_b = [];
bond_type_id = [];
bond_type_id_mapping = {};
if len(bond_nodes) == 1:
	bond = bond_nodes[0];
	bond_text = bond.childNodes[0].data.encode();
	bond_raw = bond_text.split();
	
	# loop through the bonds and split the a,b and type from the raw stream
	# map types names to numbers along the way
	for i in xrange(0,len(bond_raw),3):
		bond_a.append(bond_raw[i+1]);
		bond_b.append(bond_raw[i+2]);
		
		# use the exising mapping if we have made one
		name = bond_raw[i];
		if name in bond_type_id_mapping:
			bond_type_id.append(bond_type_id_mapping[name]);
		else:
			# otherwise, we need to create a new mapping
			bond_type_id_mapping[name] = len(bond_type_id_mapping)+1;
			bond_type_id.append(bond_type_id_mapping[name]);
			
	print "Found", len(bond_a), "bonds";
	print "Mapped bond types:"
	print bond_type_id_mapping;
	
# now we have everything and can start writing the LAMMPS output file
f = file(sys.argv[2], 'w');
f.write("File generated by hoomd2lammps_input.py %s\n" % sys.argv[1]);
f.write("%d atoms\n" % (len(xyz)/3));
f.write("%d bonds\n" % len(bond_a));
f.write("%d atom types\n" % len(type_id_mapping));
f.write("%d bond types\n" % len(bond_type_id_mapping));
f.write("%f %f xlo xhi\n" % (-float(Lx)/2.0, float(Lx)/2.0));
f.write("%f %f ylo yhi\n" % (-float(Ly)/2.0, float(Ly)/2.0));
f.write("%f %f zlo zhi\n" % (-float(Lz)/2.0, float(Lz)/2.0));
f.write("\n");
f.write("Masses\n");
f.write("\n");
for i in xrange(0,len(type_id_mapping)):
	f.write("%d 1.0\n" % (i+1));
f.write("\n");	
f.write("Atoms\n");
f.write("\n");
for i in xrange(0,len(xyz)/3):
	f.write("%d 1 %d %f %f %f\n" % (i+1, type_id[i], float(xyz[i*3]), float(xyz[i*3+1]), float(xyz[i*3+2])));
if len(velocity_xyz) > 0:
	f.write("\n");	
	f.write("Velocities\n");
	f.write("\n");
	for i in xrange(0,len(velocity_xyz)/3):
		f.write("%d %f %f %f\n" % (i+1, float(velocity_xyz[i*3]), float(velocity_xyz[i*3+1]), float(velocity_xyz[i*3+2])));
if len(bond_a) > 0:
	f.write("\n");
	f.write("Bonds\n");
	f.write("\n");
	for i in xrange(0,len(bond_a)):
		f.write("%d %d %d %d\n" % (i+1, bond_type_id[i], int(bond_a[i])+1, int(bond_b[i])+1));
f.close()

