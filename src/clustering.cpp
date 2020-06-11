/*
 * clustering.cpp
 *  
 *  Created on: 19/05/2015
 *      Author: Francesco Verdoja <francesco.verdoja@aalto.fi>
 *
 *
 * BSD 3-Clause License
 * 
 * Copyright (c) 2015, Francesco Verdoja
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * 
 * * Neither the name of the copyright holder nor the names of its 
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "supervoxel_clustering/clustering.h"

/**
 * Test if two regions form a convex angle between them
 * 
 * @param norm1     the normal of the first region
 * @param centroid1 the centroid of the first region
 * @param norm2     the normal of the second region
 * @param centroid2 the centroid of the second region
 * 
 * @return true if the two regions form a convex angle between them or false 
 *         otherwise
 */
std::vector<Eigen::VectorXf> gmm_means_global;
std::vector<Eigen::MatrixXf> gmm_covariances_global;
std::vector<float> gmm_weights_global;

bool Clustering::is_convex(Normal norm1, PointT centroid1, Normal norm2,
        PointT centroid2) const {
    Eigen::Vector3f N1 = norm1.getNormalVector3fMap();
    Eigen::Vector3f C1 = centroid1.getVector3fMap();
    Eigen::Vector3f N2 = norm2.getNormalVector3fMap();
    Eigen::Vector3f C2 = centroid2.getVector3fMap();

    Eigen::Vector3f C = C1 - C2;
    C /= C.norm();

    float cos1 = N1.dot(C);
    float cos2 = N2.dot(C);

    return cos1 >= cos2;
}

/**
 * Computes the geometric distance delta_g between two regions
 * 
 * @param norm1     the normal of the first region
 * @param centroid1 the centroid of the first region
 * @param norm2     the normal of the second region
 * @param centroid2 the centroid of the second region
 * 
 * @return the distance value
 */
float Clustering::normals_diff(Normal norm1, PointT centroid1, Normal norm2,
        PointT centroid2) const {
    Eigen::Vector3f N1 = norm1.getNormalVector3fMap();
    Eigen::Vector3f C1 = centroid1.getVector3fMap();
    Eigen::Vector3f N2 = norm2.getNormalVector3fMap();
    Eigen::Vector3f C2 = centroid2.getVector3fMap();

    Eigen::Vector3f C = C1 - C2;
    C /= C.norm();

    float N1xN2 = N1.cross(N2).norm();
    float N1_C = std::abs(N1.dot(C));
    float N2_C = std::abs(N2.dot(C));

    float delta_g = (N1xN2 + N1_C + N2_C) / 3;

    return delta_g;
}

/**
 * Computes the average friction coefficient for a regions
 * 
 * @param supvox the region
 * 
 * @return the average friction coefficient
 */
FrictionEstimateT Clustering::average_friction(Supervoxel::Ptr supvox, HapticTrackT track) const {
    pcl::PointCloud<pcl::PointXYZI>::Ptr subtrack(new pcl::PointCloud<pcl::PointXYZI>());
    float count, mean_f;
    count = mean_f = 0;
    
    if(!track.empty()) {
        PointCloudT::Ptr v = supvox->voxels_;
        for (auto const &iter : v->points) {
            HapticTrackT::const_iterator h_it = 
                    track.find(pcl::PointXYZ(iter.x, iter.y, iter.z));
            
            if (h_it != track.end()) {
                float f_x = h_it->second[0];
                float f_z = h_it->second[1];
                float f = abs(f_x/f_z);
                count++;

                mean_f = mean_f + (1 / count) * (f - mean_f);
                std::cout << "[" << count << "] f: " << f << " - mean_f: " << mean_f << std::endl; //TODO: remove after debug
                pcl::PointXYZI p(f);
                p.x = iter.x;
                p.y = iter.y;
                p.z = iter.z;
                subtrack->push_back(p);
            }
        }
        if(count != 0) {
            if (mean_f != 0) std::cout << mean_f << std::endl; //TODO: remove after debug
            
            if (mean_f < 0)
                mean_f = 0;
        }
    }

    FrictionEstimateT ret(subtrack, mean_f);
    return ret;
}

/**
 * Compute the color difference delta_c and the geometric difference delta_g for
 * two regions
 * 
 * @param supvox1   the first region
 * @param supvox2   the second region
 * 
 * @return a pair containing delta_c as first value and delta_g as second value
 */
std::array<float,3> Clustering::delta_c_g_h(Supervoxel::Ptr supvox1,
        Supervoxel::Ptr supvox2) const {
    float delta_c = 0;
    float * rgb1 = ColorUtilities::mean_color(supvox1);
    float * rgb2 = ColorUtilities::mean_color(supvox2);
    switch (delta_c_type) {
        case LAB_CIEDE00:
            float *lab1, *lab2;
            lab1 = ColorUtilities::rgb2lab(rgb1);
            lab2 = ColorUtilities::rgb2lab(rgb2);
            delta_c = ColorUtilities::lab_ciede00(lab1, lab2);
            delta_c /= LAB_RANGE;
            break;
        case RGB_EUCL:
            delta_c = ColorUtilities::rgb_eucl(rgb1, rgb2);
            delta_c /= RGB_RANGE;
    }

    float delta_g = 0;
    Normal n1 = supvox1->normal_;
    Normal n2 = supvox2->normal_;
    PointT c1 = supvox1->centroid_;
    PointT c2 = supvox2->centroid_;
    switch (delta_g_type) {
        case NORMALS_DIFF:
            delta_g = normals_diff(n1, c1, n2, c2);
            break;
        case CONVEX_NORMALS_DIFF:
            delta_g = normals_diff(n1, c1, n2, c2);
            if (is_convex(n1, c1, n2, c2))
                delta_g *= 0.5;
    }

    float delta_h = 0;
    float f1 = supvox1->friction_;
    float f2 = supvox2->friction_;
    switch (delta_h_type) {
    case AVERAGE_FRICTION:
        delta_h = std::abs(f1 - f2);
    }

    std::array<float,3> ret{delta_c, delta_g, delta_h};
    return ret;
}

/**
 * Compute the delta distance between two regions
 * 
 * @param supvox1   the first region
 * @param supvox2   the second region
 * 
 * @return the distance value
 */
float Clustering::delta(Supervoxel::Ptr supvox1,
        Supervoxel::Ptr supvox2) const {
    std::array<float,3> deltas = delta_c_g_h(supvox1, supvox2);
    
    float delta = t_c(deltas[0]) + t_g(deltas[1]) + t_h(deltas[2]);
    //printf("delta_c = %f | delta_g = %f | delta_h = %f | delta = %f\n", 
    //       deltas[0], deltas[1], deltas[2], delta);
    
    return delta;
}

/**
 * Converts a weight map into an anjacency map (i.e. a map only recording which 
 * regions are adjacent to which others without any weight information)
 * 
 * @param w_map a weight map
 * 
 * @return the corresponding adjacency map
 */
AdjacencyMapT Clustering::weight2adj(WeightMapT w_map) const {
    AdjacencyMapT adj_map;

    WeightMapT::iterator it = w_map.begin();
    WeightMapT::iterator it_end = w_map.end();
    for (; it != it_end; ++it) {
        adj_map.insert(it->second);
    }

    return adj_map;
}

/**
 * Converts an adjacency map (i.e. a map only recording which regions are 
 * adjacent to which others without any weight information) into a weight map.
 * All weight are set to -1.
 * 
 * @param adj_map an adjacency map
 * 
 * @return the corresponding weight map
 */
WeightMapT Clustering::adj2weight(AdjacencyMapT adj_map) const {
    WeightMapT w_map;

    AdjacencyMapT::iterator it = adj_map.begin();
    AdjacencyMapT::iterator it_end = adj_map.end();
    for (; it != it_end; ++it) {
        WeightedPairT elem;
        elem.first = -1;
        elem.second = *(it);
        w_map.insert(elem);
    }

    return w_map;
}

ClusteringT Clustering::estimate_frictions_and_statistics(PCLClusteringT segm, 
        HapticTrackT track) const {
    ClusteringT new_segm;
    for(auto iter : segm) {
        uint32_t label = iter.first;
        Supervoxel::Ptr s(new Supervoxel(*iter.second));
        FrictionEstimateT f = average_friction(s, track);
        s->frictions_ = f.first;
        s->friction_ = f.second;
        s->compute_statistics();
        std::pair<uint32_t, Supervoxel::Ptr> elem(label, s);
        new_segm.insert(elem);
    }
    estimate_missing_frictions(&new_segm);
    return new_segm;
}

void Clustering::estimate_missing_frictions(ClusteringT *segmentation) const {
    float count = 0;
    float total_count = 0;
    float colorpoint_per_count = 0;
    float fricpoint_per_count = 0;
    // Build the GMM
    GMM_GMR gmmnode;
    int n_rows = 100; //0.03 seed ok
    Eigen::Vector4f mean_background, last_mean_background; 
    mean_background << 0,0,0,0;
    Eigen::Matrix4f covariance_background;
    float rr, rg, rb, rf, gg, gb, gf, bb, bf, ff;
    rr = rg = rb = rf = gg = gb = gf = bb = bf = ff = 0;
    typename pcl::PointCloud<PointT>::iterator v_itr, v_itr_end;
    typename pcl::PointCloud<pcl::PointXYZI>::iterator f_itr, f_itr_end;
    std::vector<Eigen::MatrixXf> gmm_all_sample;
    for(auto iter : *segmentation) {
        // Calculate mean and cov for background class
        total_count++;
        last_mean_background.head(3) = mean_background.head(3);
        mean_background.head(3) = mean_background.head(3) + (1 / total_count) * (iter.second->mean_.head(3) - mean_background.head(3));
        v_itr = iter.second->voxels_->begin();
        v_itr_end = iter.second->voxels_->end();
        for (; v_itr != v_itr_end; ++v_itr) {

            rr = rr + (v_itr->r - last_mean_background(0)) * (v_itr->r - mean_background(0));
            gg = gg + (v_itr->g - last_mean_background(1)) * (v_itr->g - mean_background(1));
            bb = bb + (v_itr->b - last_mean_background(2)) * (v_itr->b - mean_background(2));
            rg = rg + (v_itr->r - last_mean_background(0)) * (v_itr->g - mean_background(1));
            rb = rb + (v_itr->r - last_mean_background(0)) * (v_itr->b - mean_background(2));
            gb = gb + (v_itr->g - last_mean_background(1)) * (v_itr->b - mean_background(2));
            colorpoint_per_count++;
        }
    }
    for(auto iter : *segmentation) {
        float f = iter.second->friction_;
        if(f != 0) {
            Eigen::EigenMultivariateNormal<float> normX_solver(iter.second->mean_,iter.second->covariance_);
            Eigen::MatrixXf gmm_indata(n_rows,4);
            gmm_indata << normX_solver.samples(n_rows).transpose();
            gmm_all_sample.push_back(gmm_indata);
            count++;
            // // Calculate mean and cov for background class
            last_mean_background(3) = mean_background(3);
            mean_background(3) = mean_background(3) + (1 / count) * (iter.second->mean_(3) - mean_background(3));
            // last_mean_background = mean_background;
            // mean_background = mean_background + (1 / count) * (iter.second->mean_ - mean_background);
            // v_itr = iter.second->voxels_->begin();
            // v_itr_end = iter.second->voxels_->end();
            // for (; v_itr != v_itr_end; ++v_itr) {
            //     rr = rr + (v_itr->r - last_mean_background(0)) * (v_itr->r - mean_background(0));
            //     gg = gg + (v_itr->g - last_mean_background(1)) * (v_itr->g - mean_background(1));
            //     bb = bb + (v_itr->b - last_mean_background(2)) * (v_itr->b - mean_background(2));
            //     rg = rg + (v_itr->r - last_mean_background(0)) * (v_itr->g - mean_background(1));
            //     rb = rb + (v_itr->r - last_mean_background(0)) * (v_itr->b - mean_background(2));
            //     gb = gb + (v_itr->g - last_mean_background(1)) * (v_itr->b - mean_background(2));
            //     colorpoint_per_count++;
            // }
            std::vector<int> nn_id(1);
            std::vector<float> nn_squared_dist(1);
            pcl::KdTreeFLANN<PointT> kdtree;
            kdtree.setInputCloud(iter.second->voxels_);
            f_itr = iter.second->frictions_->begin();
            f_itr_end = iter.second->frictions_->end();
            for (; f_itr != f_itr_end; ++f_itr) {
                PointT p;
                p.x = f_itr->x;
                p.y = f_itr->y;
                p.z = f_itr->z;
                if(kdtree.nearestKSearch(p, 1, nn_id, nn_squared_dist) > 0) {
                    for(std::size_t i = 0; i < nn_id.size (); ++i) {
                    PointT nn_p = iter.second->voxels_->points[ nn_id[i] ];
                        // rf = rf + (nn_p.r - last_mean_background(0)) * (nn_p.r - mean_background(0));
                        // gf = gf + (nn_p.g - last_mean_background(1)) * (nn_p.g - mean_background(1));
                        // bf = bf + (nn_p.b - last_mean_background(2)) * (nn_p.b - mean_background(2));
                        ff = ff + (f_itr->intensity -last_mean_background(3)) * (f_itr->intensity - mean_background(3));
                        fricpoint_per_count++;

                    }
                }
            }
        }
    }
    rr = rr / (colorpoint_per_count);
    gg = gg / (colorpoint_per_count);
    bb = bb / (colorpoint_per_count);
    rg = rg / (colorpoint_per_count);
    rb = rb / (colorpoint_per_count);
    gb = gb / (colorpoint_per_count);
    rf = rf / (fricpoint_per_count);
    gf = gf / (fricpoint_per_count);
    bf = bf / (fricpoint_per_count);
    ff = ff / (fricpoint_per_count);
    covariance_background << rr, rg, rb, 1, 
                            rg, gg, gb, 1,
                            rb, gb, bb, 1,
                            1, 1, 1, 1;

    std::cout << "color: " << colorpoint_per_count << " -- fric count: " << fricpoint_per_count << std::endl;
    std::cout << "mean background: " << mean_background << std::endl;
    std::cout << "cov background: " << covariance_background << std::endl;

    Eigen::MatrixXf gmm_input;
    gmm_input = gmmnode.VStack(gmm_all_sample);
    std::cout << "Number of touched points: " << count << std::endl;
    std::cout << "Size: " << gmm_input.innerSize()  << gmm_input.outerSize() << std::endl;
    auto gmm_variables = gmmnode.fit_gmm(gmm_input);
    auto gmm_means = std::get<0>(gmm_variables);
    auto gmm_covariances = std::get<1>(gmm_variables);
    auto gmm_weights_pre = std::get<2>(gmm_variables);
    std::vector<float> gmm_weights;
    if (gmm_weights_pre.size() == 2){
        gmm_means.push_back(mean_background);
        gmm_covariances.push_back(covariance_background);
        for (auto i: gmm_weights_pre){
            i = i*(1-0.2);
            gmm_weights.push_back(i);
        }
        gmm_weights.push_back(0.2);
    }
    else{
        gmm_weights = gmm_weights_pre;
    }
    gmm_means_global = gmm_means;
    gmm_covariances_global = gmm_covariances;
    gmm_weights_global = gmm_weights;

    // Estimate through GMR
    for(auto iter : *segmentation) {
        if(iter.second->friction_ == 0) {
            Eigen::MatrixXf x(1,3);
            x = iter.second->mean_.head(3).transpose();
            auto predicted_values = gmmnode.gmr(gmm_weights,gmm_means,gmm_covariances,x,3,1);
            iter.second->friction_ = std::get<0>(predicted_values)[0];
            iter.second->friction_variance_ = std::get<1>(predicted_values)(0,0);
            // auto predicted_covariances = std::get<1>(predicted_values)(0,0);
            // if (iter.second->friction_ < 0){
            //     iter.second->friction_ = abs(iter.second->friction_);
            // }
            if (iter.second->friction_ >= 1){
                iter.second->friction_ = iter.second->friction_ - iter.second->friction_variance_;
            }
            std::cout << "Mean input: " << x << " -- Fric: " << iter.second->friction_ << " -- Cov: " << iter.second->friction_variance_ << std::endl;
        }
    }
    for (auto i: gmm_weights)
        std::cout << "weights: "<< i << std::endl;
    for (auto i: gmm_means)
        std::cout << "means: "<< i << std::endl;

    for (auto i: gmm_covariances)
        std::cout << "covariances: "<< i << std::endl;

}

/**
 * Initialize all weights in the initial state of the graph
 */
void Clustering::init_weights() {
    std::map<std::string, std::array<float,3> > temp_deltas;
    DeltasDistribT deltas_c;
    DeltasDistribT deltas_g;
    DeltasDistribT deltas_h;
    WeightMapT w_new;

    WeightMapT::iterator it = initial_state.weight_map.begin();
    WeightMapT::iterator it_end = initial_state.weight_map.end();
    for (; it != it_end; ++it) {
        uint32_t sup1_id = it->second.first;
        uint32_t sup2_id = it->second.second;
        std::stringstream ids;
        ids << sup1_id << "-" << sup2_id;
        Supervoxel::Ptr sup1 = initial_state.segments.at(sup1_id);
        Supervoxel::Ptr sup2 = initial_state.segments.at(sup2_id);
        std::array<float,3> deltas = delta_c_g_h(sup1, sup2);
        temp_deltas.insert(
                std::pair<std::string, std::array<float,3>>(ids.str(),deltas));
        deltas_c.insert(deltas[0]);
        deltas_g.insert(deltas[1]);
        deltas_h.insert(deltas[2]);
    }

    init_merging_parameters(deltas_c, deltas_g, deltas_h);

    it = initial_state.weight_map.begin();
    for (; it != it_end; ++it) {
        uint32_t sup1_id = it->second.first;
        uint32_t sup2_id = it->second.second;
        std::stringstream ids;
        ids << sup1_id << "-" << sup2_id;
        std::array<float,3> deltas = temp_deltas.at(ids.str());
        float delta = t_c(deltas[0]) + t_g(deltas[1]) + t_h(deltas[2]);
        w_new.insert(WeightedPairT(delta, it->second));
    }

    initial_state.set_weight_map(w_new);

    init_initial_weights = true;
}

/**
 * Initialize the parameters of the merging approach based on the statistical 
 * distributions of delta_c and delta_g
 * 
 * @param deltas_c the distribution of delta_c values
 * @param deltas_g the distribution of delta_g values
 */
void Clustering::init_merging_parameters(DeltasDistribT deltas_c,
        DeltasDistribT deltas_g, DeltasDistribT deltas_h) {
    switch (merging_type) {
        case MANUAL_LAMBDA:
        {
            break;
        }
        case ADAPTIVE_LAMBDA:
        {
            float mean_c = deltas_mean(deltas_c);
            float mean_g = deltas_mean(deltas_g);
            float mean_h = deltas_mean(deltas_h);
            // lambda_c = (mean_g * mean_h) / 
            //           (mean_c * mean_g + mean_g * mean_h + mean_c * mean_h);
            // lambda_g = lambda_c * mean_c / mean_g;
            lambda_c = mean_h / (mean_c + mean_h);
            lambda_g = 0;
            break;
        }
        case EQUALIZATION:
        {
            cdf_c = compute_cdf(deltas_c);
            cdf_g = compute_cdf(deltas_g);
            cdf_h = compute_cdf(deltas_h);
        }
    }
}

/**
 * Compute the cumulative distribution function (cdf) for the given distribution
 * 
 * @param dist  a sampled distribution
 * 
 * @return the cdf of the given distribution
 */
std::map<short, float> Clustering::compute_cdf(DeltasDistribT dist) {
    std::map<short, float> cdf;
    int bins[bins_num] = {};

    DeltasDistribT::iterator d_itr, d_itr_end;
    d_itr = dist.begin();
    d_itr_end = dist.end();
    int n = dist.size();
    for (; d_itr != d_itr_end; ++d_itr) {
        float d = *d_itr;
        short bin = std::floor(d * bins_num);
        if (bin == bins_num)
            bin--;
        bins[bin]++;
    }

    for (short i = 0; i < bins_num; i++) {
        float v = 0;
        for (short j = 0; j <= i; j++)
            v += bins[j];
        v /= n;
        cdf.insert(std::pair<short, float>(i, v));
    }

    return cdf;
}

/**
 * Transform the color distance according to the chosen unification 
 * transformation
 * 
 * @param delta_c   the color ditance value
 * 
 * @return the transformed value
 */
float Clustering::t_c(float delta_c) const {
    float ret = 0;
    switch (merging_type) {
        case MANUAL_LAMBDA:
        {
            ret = lambda_c * delta_c;
            break;
        }
        case ADAPTIVE_LAMBDA:
        {
            ret = lambda_c * delta_c;
            break;
        }
        case EQUALIZATION:
        {
            short bin = std::floor(delta_c * bins_num);
            if (bin == bins_num)
                bin--;
            ret = cdf_c.at(bin) / 3;
        }
    }
    return ret;
}

/**
 * Transform the geometric distance according to the chosen unification 
 * transformation
 * 
 * @param delta_g   the geometric ditance value
 * 
 * @return the transformed value
 */
float Clustering::t_g(float delta_g) const {
    float ret = 0;
    switch (merging_type) {
        case MANUAL_LAMBDA:
        {
            ret = lambda_g * delta_g;
            break;
        }
        case ADAPTIVE_LAMBDA:
        {
            ret = lambda_g * delta_g;
            break;
        }
        case EQUALIZATION:
        {
            short bin = std::floor(delta_g * bins_num);
            if (bin == bins_num)
                bin--;
            ret = cdf_g.at(bin) / 3;
        }
    }
    return ret;
}

/**
 * Transform the haptic distance according to the chosen unification 
 * transformation
 * 
 * @param delta_h   the haptic ditance value
 * 
 * @return the transformed value
 */
float Clustering::t_h(float delta_h) const {
    float ret = 0;
    switch (merging_type) {
        case MANUAL_LAMBDA:
        {
            ret = (1 - lambda_c - lambda_g) * delta_h;
            break;
        }
        case ADAPTIVE_LAMBDA:
        {
            ret = (1 - lambda_c - lambda_g) * delta_h;
            break;
        }
        case EQUALIZATION:
        {
            short bin = std::floor(delta_h * bins_num);
            if (bin == bins_num)
                bin--;
            ret = cdf_h.at(bin) / 3;
        }
    }
    return ret;
}

/**
 * Perform the clustering. The result is stored in the object internal state.
 * 
 * @param start     the state from which the clustering should start
 * @param threshold the threshold value
 */
void Clustering::cluster(ClusteringState start, float threshold) {
    state = start;

    WeightedPairT next;
    while (!state.weight_map.empty()
            && (next = state.get_first_weight(), next.first < threshold)) {
        pcl::console::print_debug("left: %de/%dp - w: %f - [%d, %d]...",
                state.weight_map.size(), state.segments.size(), next.first,
                next.second.first, next.second.second);
        merge(next.second);
        pcl::console::print_debug("OK\n");
    }
}

/**
 * Merge two regions into one
 * 
 * @param supvox_ids    a pair containing the two region labels to be merged
 */
void Clustering::merge(std::pair<uint32_t, uint32_t> supvox_ids) {
    Supervoxel::Ptr sup1 = state.segments.at(supvox_ids.first);
    Supervoxel::Ptr sup2 = state.segments.at(supvox_ids.second);
    Supervoxel::Ptr sup_new = boost::make_shared<Supervoxel>();

    *(sup_new->voxels_) = *(sup1->voxels_) + *(sup2->voxels_);
    *(sup_new->normals_) = *(sup1->normals_) + *(sup2->normals_);

    PointT new_centr;
    computeCentroid(*(sup_new->voxels_), new_centr);
    sup_new->centroid_ = new_centr;

    Eigen::Vector4f new_norm;
    float new_curv;
    int count = 0;
    computePointNormal(*(sup_new->voxels_), new_norm, new_curv);
    flipNormalTowardsViewpoint(sup_new->centroid_, 0, 0, 0, new_norm);
    new_norm[3] = 0.0f;
    new_norm.normalize();
    sup_new->normal_.normal_x = new_norm[0];
    sup_new->normal_.normal_y = new_norm[1];
    sup_new->normal_.normal_z = new_norm[2];
    sup_new->normal_.curvature = new_curv;

    *(sup_new->frictions_) = *(sup1->frictions_) + *(sup2->frictions_);
    if (sup_new->frictions_->size() != 0) {
        sup_new->friction_ = (sup1->frictions_->size() * sup1->friction_ +
                             sup2->frictions_->size() * sup2->friction_) /
                             sup_new->frictions_->size();
        //sup_new->compute_statistics(); // TODO: uncomment after covariance calculation is fixed
    } else {
        // sup_new->compute_statistics(); // TODO: uncomment after covariance calculation is fixed
        // float * rgb_supnew = ColorUtilities::mean_color(sup_new);
        // GMM_GMR gmmnode_newsup;
        // Eigen::MatrixXf x(1,3);
        // // x = sup_new->mean_.head(3).transpose();
        // x << rgb_supnew[0],rgb_supnew[1],rgb_supnew[2];
        // auto supnew_predicted_values = gmmnode_newsup.gmr(gmm_weights_global,gmm_means_global,gmm_covariances_global,x,3,1);
        // sup_new->friction_ = std::get<0>(supnew_predicted_values)[0];
        // std::cout << "sup new: " << x << "fric: " << sup_new->friction_ << std::endl;
        sup_new->friction_ = (sup1->friction_+sup2->friction_)/2;
        auto delta = delta_c_g_h(sup1,sup2);
        std::cout << "sup1: " << sup1->friction_ << " -- sup2: "  << sup2->friction_ <<  " fric: " << sup_new->friction_ << " delta_c: " << delta[0] << "-- delta_h: "<<delta[2] <<std::endl;
 
        // std::cout << "sup new: " << x << "fric: " << sup_new->friction_ << std::endl;
        // TODO: GMR?
    }
    
    // the two regions were both touched    FINE
    // only one is touched                  FINE?
    // neither was touched                  GMR?

    state.segments.erase(supvox_ids.first);
    state.segments.erase(supvox_ids.second);
    state.segments.insert(
            std::pair<uint32_t, Supervoxel::Ptr>(supvox_ids.first, sup_new));

    WeightMapT new_map;

    WeightMapT::iterator it = state.weight_map.begin();
    ++it;
    WeightMapT::iterator it_end = state.weight_map.end();
    for (; it != it_end; ++it) {
        std::pair<uint32_t, uint32_t> curr_ids = it->second;
        if (curr_ids.first == supvox_ids.first
                || curr_ids.second == supvox_ids.first) {
            if (!contains(new_map, curr_ids.first, curr_ids.second)) {
                float w = delta(state.segments.at(curr_ids.first),
                        state.segments.at(curr_ids.second));
                new_map.insert(WeightedPairT(w, curr_ids));
            }
        } else if (curr_ids.first == supvox_ids.second) {
            curr_ids.first = supvox_ids.first;
            if (!contains(new_map, curr_ids.first, curr_ids.second)) {
                float w = delta(state.segments.at(curr_ids.first),
                        state.segments.at(curr_ids.second));
                new_map.insert(WeightedPairT(w, curr_ids));
            }
        } else if (curr_ids.second == supvox_ids.second) {
            if (curr_ids.first < supvox_ids.first)
                curr_ids.second = supvox_ids.first;
            else {
                curr_ids.second = curr_ids.first;
                curr_ids.first = supvox_ids.first;
            }
            if (!contains(new_map, curr_ids.first, curr_ids.second)) {
                float w = delta(state.segments.at(curr_ids.first),
                        state.segments.at(curr_ids.second));
                new_map.insert(WeightedPairT(w, curr_ids));
            }
        } else {
            new_map.insert(*it);
        }
    }
    state.weight_map = new_map;
}

/**
 * Clear the lower triangle under the diaconal of the adjacency map
 * 
 * @param adjacency the adjacency map to be cleared
 */
void Clustering::clear_adjacency(AdjacencyMapT * adjacency) {
    AdjacencyMapT::iterator it = adjacency->begin();
    AdjacencyMapT::iterator it_end = adjacency->end();
    while (it != it_end) {
        if (it->first > it->second) {
            adjacency->erase(it++);
        } else {
            ++it;
        }
    }
}

/**
 * Check if the edge connecting two regions is contained in a weight map
 * 
 * @param w     a weight map
 * @param i1    the first label
 * @param i2    the second label
 * 
 * @return true if the edge exists, false if it doesn't
 */
bool Clustering::contains(WeightMapT w, uint32_t i1, uint32_t i2) {
    WeightMapT::iterator it = w.begin();
    WeightMapT::iterator it_end = w.end();
    for (; it != it_end; ++it) {
        std::pair<uint32_t, uint32_t> ids = it->second;
        if (ids.first == i1 && ids.second == i2)
            return true;
    }
    return false;
}

/**
 * Compute the mean of a distribution
 * 
 * @param deltas    a distribution
 * 
 * @return the mean value
 */
float Clustering::deltas_mean(DeltasDistribT deltas) {
    DeltasDistribT::iterator d_itr, d_itr_end;
    d_itr = deltas.begin();
    d_itr_end = deltas.end();
    float count = 0;
    float mean_d = 0;
    for (; d_itr != d_itr_end; ++d_itr) {
        float delta = *d_itr;
        count++;

        mean_d = mean_d + (1 / count) * (delta - mean_d);
    }
    return mean_d;
}

/**
 * The default constructor
 */
Clustering::Clustering() {
    set_delta_c(LAB_CIEDE00);
    set_delta_g(NORMALS_DIFF);
    set_delta_h(AVERAGE_FRICTION);
    set_merging(ADAPTIVE_LAMBDA);
    set_initial_state = false;
    init_initial_weights = false;
}

/**
 * A constructor initializing all parameters to given values
 * 
 * @param c the color distance type
 * @param g the geometric distance type
 * @param h the haptic distance type
 * @param m the merging approach type
 */
Clustering::Clustering(ColorDistance c, GeometricDistance g, HapticDistance h,
        MergingCriterion m) {
    set_delta_c(c);
    set_delta_g(g);
    set_delta_h(h);
    set_merging(m);
    set_initial_state = false;
    init_initial_weights = false;
}

/**
 * Set the merging approach type
 * 
 * @param m a merging approach type
 */
void Clustering::set_merging(MergingCriterion m) {
    merging_type = m;
    // lambda_c = 1 / 3;
    lambda_c = 0.5;
    // lambda_g = 1 / 3;
    bins_num = 500;
    init_initial_weights = false;
}

/**
 * Set the value of lambda
 * 
 * @param l the value of lambda
 */
void Clustering::set_lambda(std::pair<float, float> l) {
    if (merging_type != MANUAL_LAMBDA)
        throw std::logic_error(
            "Lambdas can be set only if the merging criterion is set to MANUAL_LAMBDA");
    if (l.first < 0 || l.first > 1 || l.second < 0 || l.second > 1 || 
       l.first + l.second > 1)
        throw std::invalid_argument("Argument lambda outside range [0, 1]");
    lambda_c = l.first;
    lambda_g = l.second;
    init_initial_weights = false;
}

/**
 * Set the number of bins for the equalization
 * 
 * @param b the number of bins
 */
void Clustering::set_bins_num(short b) {
    if (merging_type != EQUALIZATION)
        throw std::logic_error(
            "Bins number can be set only if the merging criterion is set to EQUALIZATION");
    if (b < 0)
        throw std::invalid_argument("Argument lower than 0");
    bins_num = b;
    init_initial_weights = false;
}

/**
 * Set the initial state of the clustering process with no haptic information
 * 
 * @param segm  the initial segmentation (output of some supervoxel algorithm)
 * @param adj   the edges (unweighted) of the clustering graph
 */
void Clustering::set_initialstate(PCLClusteringT segm, AdjacencyMapT adj) {
    HapticTrackT track;
    set_initialstate(estimate_frictions_and_statistics(segm, track), adj);
}

/**
 * Set the initial state of the clustering process with haptic information
 * 
 * @param segm  the initial segmentation (output of some supervoxel algorithm)
 * @param adj   the edges (unweighted) of the clustering graph
 * @param track the haptic track performed by the robot
 */
void Clustering::set_initialstate(PCLClusteringT segm, AdjacencyMapT adj, HapticTrackT track) {
    set_initialstate(estimate_frictions_and_statistics(segm, track), adj);
}

/**
 * Set the initial state of the clustering process
 * 
 * @param segm  the initial segmentation (output of some supervoxel algorithm, 
 *              alredy converted to include friction data)
 * @param adj   the edges (unweighted) of the clustering graph
 */
void Clustering::set_initialstate(ClusteringT segm, AdjacencyMapT adj) {
    clear_adjacency(&adj);
    ClusteringState init_state(segm, adj2weight(adj));
    initial_state = init_state;
    state = init_state;
    set_initial_state = true;
    init_initial_weights = false;
}

/**
 * Get the current state of the segmentation
 * 
 * @return the current segmentation
 */
std::pair<ClusteringT, AdjacencyMapT> Clustering::get_currentstate() const {
    std::pair<ClusteringT, AdjacencyMapT> ret;
    ret.first = state.segments;
    ret.second = weight2adj(state.weight_map);
    return ret;
}

/**
 * Get the colored pointcloud corresponding to the current state
 * 
 * @return a colored pointcloud
 */
PointCloudT::Ptr Clustering::get_colored_cloud() const {
    return label2color(get_labeled_cloud());
}

/**
 * Get the pointcloud of the regions corresponding to the current state
 * 
 * @return a labelled pointcloud
 */
PointLCloudT::Ptr Clustering::get_labeled_cloud() const {
    PointLCloudT::Ptr label_cloud(new PointLCloudT);

    ClusteringT::const_iterator it = state.segments.begin();
    ClusteringT::const_iterator it_end = state.segments.end();

    uint32_t current_l = 0;
    for (; it != it_end; ++it) {
        PointCloudT cloud = *(it->second->voxels_);
        PointCloudT::iterator it_cloud = cloud.begin();
        PointCloudT::iterator it_cloud_end = cloud.end();
        for (; it_cloud != it_cloud_end; ++it_cloud) {
            PointLT p;
            p.x = it_cloud->x;
            p.y = it_cloud->y;
            p.z = it_cloud->z;
            p.label = current_l;
            label_cloud->push_back(p);
        }
        current_l++;
    }

    return label_cloud;
}

/**
 * Get the pointcloud of the regions corresponding to the current state
 * 
 * @return a labelled pointcloud
 */
PointCloudT::Ptr Clustering::get_friction_cloud() const {
    PointCloudT::Ptr friction_cloud(new PointCloudT);

    ClusteringT::const_iterator it = state.segments.begin();
    ClusteringT::const_iterator it_end = state.segments.end();

    for (; it != it_end; ++it) {
        PointCloudT cloud = *(it->second->voxels_);
        PointCloudT::iterator it_cloud = cloud.begin();
        PointCloudT::iterator it_cloud_end = cloud.end();
        for (; it_cloud != it_cloud_end; ++it_cloud) {
            PointT p;
            p.x = it_cloud->x;
            p.y = it_cloud->y;
            p.z = it_cloud->z;
            p.r = it->second->friction_ * 255;
            p.g = 0;
            p.b = 50;
            friction_cloud->push_back(p);
        }
    }

    return friction_cloud;
}

/**
 * Get the pointcloud of the regions corresponding to the current state
 * 
 * @return a labelled pointcloud
 */
PointCloudT::Ptr Clustering::get_uncertainty_cloud() const {
    PointCloudT::Ptr uncertainty_cloud(new PointCloudT);

    ClusteringT::const_iterator it = state.segments.begin();
    ClusteringT::const_iterator it_end = state.segments.end();

    for (; it != it_end; ++it) {
        PointCloudT cloud = *(it->second->voxels_);
        PointCloudT::iterator it_cloud = cloud.begin();
        PointCloudT::iterator it_cloud_end = cloud.end();
        for (; it_cloud != it_cloud_end; ++it_cloud) {
            PointT p;
            p.x = it_cloud->x;
            p.y = it_cloud->y;
            p.z = it_cloud->z;
            p.r = 0;
            p.g = it->second->friction_variance_ * 255;
            p.b = 0;
            uncertainty_cloud->push_back(p);
        }
    }

    return uncertainty_cloud;
}

/**
 * Perform the clustering
 * 
 * @param threshold the stopping threshold for the clustering process
 */
void Clustering::cluster(float threshold) {
    if (!set_initial_state)
        throw std::logic_error("Cannot call 'cluster' before "
            "setting an initial state with 'set_initialstate'");

    if (!init_initial_weights)
        init_weights();
    std::cout << "Weight initialized" << std::endl;
    
    cluster(initial_state, threshold);
}

/**
 * Perform the clustering testing all possible thresholds in a range
 * 
 * @param ground_truth  the groundtruth
 * @param start_thresh  the starting threshold
 * @param end_thresh    the end threshold
 * @param step_thresh   the iteration step
 * 
 * @return a map collecting all metric scores for each threshold value
 */
std::map<float, performanceSet> Clustering::all_thresh(
        PointLCloudT::Ptr ground_truth, float start_thresh,
        float end_thresh, float step_thresh) {
    if (start_thresh < 0 || start_thresh > 1 || end_thresh < 0 || end_thresh > 1
            || step_thresh < 0 || step_thresh > 1) {
        throw std::out_of_range(
                "start_thresh, end_thresh and/or step_thresh outside of range [0, 1]");
    }
    if (start_thresh > end_thresh) {
        pcl::console::print_warn(
                "Start threshold greater then end threshold, inverting.\n");
        float temp = end_thresh;
        end_thresh = start_thresh;
        start_thresh = temp;
    }

    pcl::console::print_info("Testing thresholds from %f to %f (step %f)\n",
            start_thresh, end_thresh, step_thresh);

    std::map<float, performanceSet> thresholds;
    cluster(start_thresh);
    Testing test(get_labeled_cloud(), ground_truth);
    performanceSet p = test.eval_performance();
    thresholds.insert(std::pair<float, performanceSet>(start_thresh, p));
    pcl::console::print_info("<T, Fscore, voi, wov> = <%f, %f, %f, %f>\n",
            start_thresh, p.fscore, p.voi, p.wov);

    for (float t = start_thresh + step_thresh; t <= end_thresh; t +=
            step_thresh) {
        cluster(state, t);
        test.set_segm(get_labeled_cloud());
        p = test.eval_performance();
        thresholds.insert(std::pair<float, performanceSet>(t, p));
        pcl::console::print_info("<T, Fscore, voi, wov> = <%f, %f, %f, %f>\n", t,
                p.fscore, p.voi, p.wov);
    }

    return thresholds;
}

/**
 * Perform the clustering testing all possible thresholds in a range and returns
 * the best performance (according to F-score)
 * 
 * @param ground_truth  the groundtruth
 * @param start_thresh  the starting threshold
 * @param end_thresh    the end threshold
 * @param step_thresh   the iteration step
 * 
 * @return the best performance and the relative threshold
 */
std::pair<float, performanceSet> Clustering::best_thresh(
        PointLCloudT::Ptr ground_truth, float start_thresh,
        float end_thresh, float step_thresh) {
    std::map<float, performanceSet> thresholds = all_thresh(ground_truth,
            start_thresh, end_thresh, step_thresh);
    return best_thresh(thresholds);
}

/**
 * Returns the best performance (according to F-score) for a collection of 
 * threshold performances
 * 
 * @param all_tresh a collection of performance score for a range of possible 
 *                  thresholds
 * 
 * @return the best performance and the relative threshold
 */
std::pair<float, performanceSet> Clustering::best_thresh(
        std::map<float, performanceSet> all_thresh) {
    float best_t = 0;
    performanceSet best_performance;

    std::map<float, performanceSet>::iterator it = all_thresh.begin();

    for (; it != all_thresh.end(); ++it) {
        if (it->second.fscore > best_performance.fscore) {
            best_performance = it->second;
            best_t = it->first;
        }
    }

    return std::pair<float, performanceSet>(best_t, best_performance);
}

/**
 * Perform all color tests
 */
void Clustering::test_all() const {
    ColorUtilities::rgb_test();
    ColorUtilities::lab_test();
    ColorUtilities::convert_test();
}

/**
 * Convert a labelled pointcloud into a color one assigning the color in the 
 * Glasbey lookup table corresponding to the label number
 * 
 * @param label_cloud   a labelled pointcloud
 * 
 * @return the colored pointcloud
 */
PointCloudT::Ptr Clustering::label2color(
        PointLCloudT::Ptr label_cloud) {
    PointCloudT::Ptr colored_cloud(new PointCloudT);
    pcl::PointCloud<PointLCT>::Ptr temp_cloud(new pcl::PointCloud<PointLCT>);

    copyPointCloud(*label_cloud, *temp_cloud);

    pcl::PointCloud<PointLCT>::iterator it = temp_cloud->begin();
    pcl::PointCloud<PointLCT>::iterator it_end = temp_cloud->end();

    for (; it != it_end; ++it) {
        uint8_t * rgb = ColorUtilities::get_glasbey(it->label);
        it->r = rgb[0];
        it->g = rgb[1];
        it->b = rgb[2];
    }

    copyPointCloud(*temp_cloud, *colored_cloud);
    return colored_cloud;
}

/**
 * Convert a pointcloud having points colored according to their labels into a
 * labelled pointcloud assigning a label to all adjacent points having the same 
 * color
 *  
 * @param colored_cloud a colored pointcloud
 * 
 * @return a labelled pointcloud
 */
PointLCloudT::Ptr Clustering::color2label(
        PointCloudT::Ptr colored_cloud) {
    PointLCloudT::Ptr label_cloud(new PointLCloudT);
    pcl::PointCloud<PointLCT>::Ptr temp_cloud(new pcl::PointCloud<PointLCT>);
    std::map<float, uint32_t> mappings;
    copyPointCloud(*colored_cloud, *temp_cloud);

    pcl::PointCloud<PointLCT>::iterator it = temp_cloud->begin();
    pcl::PointCloud<PointLCT>::iterator it_end = temp_cloud->end();

    uint32_t i = 0;
    for (; it != it_end; ++it) {
        if (mappings.count(it->rgb) != 0)
            it->label = mappings.at(it->rgb);
        else {
            it->label = i;
            mappings.insert(std::pair<float, uint32_t>(it->rgb, i));
            i++;
        }
    }

    copyPointCloud(*temp_cloud, *label_cloud);
    return label_cloud;
}
