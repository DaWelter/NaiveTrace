import os
import sys
import json
import glob
import numpy as np
from collections import defaultdict, namedtuple

import munch

from pprint import pprint



try:
    import path_guiding
except ModuleNotFoundError:
    import imp
    path_guiding = imp.load_dynamic('path_guiding', r'..\buildwin\Debug\path_guiding.dll')

def read_records(filename):
    print (f"Opening {filename}")
    with open(filename, 'r') as f:
        return json.load(f)
    
def merge_dict_list(a, b):
    keys = set(a.keys()).union(b.keys())
    no = []
    return { k:(a.get(k,no)+b.get(k,no)) for k in keys }

#dirs vals proj 
#CellData = namedtuple('CellData', 'center size val proj dir mixture_learned mixture_sampled num_points box')

def read_gmm(gmm_json):
    gmm = path_guiding.GMM2d()
    gmm.weights = gmm_json['weights']
    gmm.means = gmm_json['means']
    gmm.precisions = gmm_json['precisions']
    return gmm

def read_movmf(data):
    m = path_guiding.VMFMixture()
    m.weights = data['weights']
    m.means   = data['means']
    m.concentrations = data['concentrations']
    return m

def read_mixture(data):
    if 'concentrations' in data:
        return read_movmf(data)
    else:
        return read_gmm(data)

def convert_(rec, load_samples):
    cd = munch.Munch(
        dir = None,
        val = None,
        proj = None,
        center = np.array(rec['point_distribution_statistics_mean']),
        stddev = np.array(rec['point_distribution_statistics_stddev']),
        size = np.array(rec['size']),
        average_weight = rec['average_weight'],
        num_points = rec['num_points'],
        mixture_learned = read_mixture(rec['mixture_learned']),
        mixture_sampled = read_mixture(rec['mixture_sampled']),
        box = None,
    )
    if load_samples:
        cellrecords = read_records(rec['filename'])
        asarray = lambda k: np.array([v[k] for v in cellrecords], dtype=np.float32)
        cellrecords = {
            k:asarray(k) for k in 'dir val proj'.split()
        }
        cd = cd.update(**cellrecords)
    return cd


def convert_data(tree_rec, load_samples = False, build_boxes = False, root_box_radius = 5.):
    records = [
        convert_(rec, load_samples) for rec in tree_rec['records']
    ]
    lookup_by_id = { r['id']:records[i] for i,r in enumerate(tree_rec['records'])}
    def fix_ref(node):
        if node['kind']=='leaf':
            node['data'] = lookup_by_id[node['id']]
            del node['id']
        else:
            fix_ref(node['left'])
            fix_ref(node['right'])
    fix_ref(tree_rec['tree'])
    if build_boxes:
        s = root_box_radius
        initial_box = np.asarray([[-s, s], [-s, s], [-s, s]])
        generate_cell_boxes(initial_box, tree_rec['tree'])
    return tree_rec['tree'], records


def split(box, branch):
    left = box.copy()
    right = box.copy()
    axis = branch['split_axis']
    pos = branch['split_pos']
    #print (axis, box[axis],pos)
    assert (box[axis][0] <= pos <= box[axis][1])
    left[axis,1] = pos
    right[axis,0] = pos
    return left, right


def generate_cell_boxes(box, node):
    if node['kind'] == 'leaf':
        node['data'].box = box
    else:
        lb, rb = split(box, node)
        generate_cell_boxes(lb, node['left'])
        generate_cell_boxes(rb, node['right'])


