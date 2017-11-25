# -*- coding: utf-8 -*-
"""
This is a quick evaluation of the ratio tracking method presented in
Kutz et al. (2017) "Spectral and Decomposition Tracking for Rendering Heterogeneous Volumes"

@author: Michael Welter <michael@welter-4d.de>
"""
from __future__ import print_function

import random
import math
import numpy as np

import mediumspec as ms
ms.init('single_box', 'const')

def exp_sample(sigma):
  """
    Generate random number t according to the prob density sigma*exp(-sigma*t)
  """
  return -math.log(random.uniform(0., 1.))/sigma


def single_lambda_ratio_tracking(lambda_):
  """
    pg. 111:16, Eq (41)
  """
  sigma_t_majorante_lambda = ms.sigma_t_majorante[lambda_]
  x = 0.
  weight = 1.
  while True:
    x += exp_sample(sigma_t_majorante_lambda)
    if x > ms.domain_length:
      return weight
    else:
      sigma_s = ms.get_sigma_s(x)[lambda_]
      sigma_a = ms.get_sigma_a(x)[lambda_]
      sigma_n = sigma_t_majorante_lambda - sigma_s - sigma_a
      weight *= sigma_n / sigma_t_majorante_lambda

def ratio_tracking():
  """
    pg. 111:16, Eq (41)
  """
  sigma_t_majorante = ms.sigma_t_majorante_across_channels
  x = 0.
  weight = np.ones(2, np.float32)
  while True:
    x += exp_sample(sigma_t_majorante)
    if x > ms.domain_length:
      return weight
    else:
      sigma_s = ms.get_sigma_s(x)
      sigma_a = ms.get_sigma_a(x)
      sigma_n = sigma_t_majorante - sigma_s - sigma_a
      weight *= sigma_n / sigma_t_majorante


def weighted_next_flight_estimator():
  """
    pg. 111:16, Eq (39)
    More variance than ratio tracking?!
  """
  sigma_t_majorante = ms.sigma_t_majorante_across_channels
  x = 0.
  weight_product = np.ones(2, np.float32)
  weight = np.zeros(2, np.float32)
  while True:
    weight += np.exp(sigma_t_majorante * (x - ms.domain_length)) * weight_product
    x += exp_sample(sigma_t_majorante)
    if x > ms.domain_length:
      return weight
    else:
      sigma_s = ms.get_sigma_s(x)
      sigma_a = ms.get_sigma_a(x)
      sigma_n = sigma_t_majorante - sigma_s - sigma_a
      weight_product *= sigma_n / sigma_t_majorante


Nsamples = 10000

samples_per_lambda = [ [] for _ in  range(2) ]
for lambda_ in  range(len(samples_per_lambda)):
  for i in range(Nsamples):
    w = single_lambda_ratio_tracking(lambda_)
    samples_per_lambda[lambda_].append(w)
samples_per_lambda = np.asarray(samples_per_lambda).T

estimate = np.average(samples_per_lambda, axis=0)
stdev    = np.std(samples_per_lambda, axis=0) / math.sqrt(len(samples_per_lambda))

print ("Single Lambda Ratio Tracking: ", estimate, " +/- ", stdev)

samples = np.asarray([ ratio_tracking() for _ in range(Nsamples)])
estimate = np.average(samples, axis=0)
stdev    = np.std(samples, axis=0) / math.sqrt(len(samples))

print ("Ratio Tracking: ", estimate, " +/- ", stdev)

samples = np.asarray([ weighted_next_flight_estimator() for _ in range(Nsamples)])
estimate = np.average(samples, axis=0)
stdev    = np.std(samples, axis=0) / math.sqrt(len(samples))

print ("Weighted Next Flight Estimator: ", estimate, " +/- ", stdev)